#include "emojineer/web.hpp"

#include "emojineer/compiler.hpp"
#include "emojineer/interop.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/unicode.hpp"
#include "emojineer/vm.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace emojineer {
namespace {

constexpr std::size_t MaxWebSourceBytes = 512 * 1024;
constexpr std::size_t MaxWebNodes = 10000;
constexpr std::size_t MaxWebDepth = 64;
constexpr std::size_t MaxBehaviorSourceBytes = 256 * 1024;
constexpr std::size_t MaxBehaviorPayloadBytes = 64 * 1024;
constexpr std::size_t MaxBehaviorResponseBytes = 1024 * 1024;
enum class BlockKind { Root, Section, Navigation, Form, List, Table };

struct Frame {
    std::vector<WebNode>* children{nullptr};
    BlockKind kind{BlockKind::Root};
};

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

[[noreturn]] void web_error(std::size_t line, const std::string& message) {
    throw std::runtime_error("web line " + std::to_string(line) + ": " + message);
}

bool same_command(const std::string& actual, const char* expected) {
    return canonicalize_token(actual) == canonicalize_token(expected);
}
std::vector<std::string> web_fields(const std::string& line, std::size_t line_number) {
    std::vector<std::string> fields;
    std::size_t cursor = 0;
    const std::string delimiter = "📜";
    while (cursor < line.size()) {
        while (cursor < line.size() &&
               std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
        if (cursor >= line.size()) break;
        if (line.compare(cursor, delimiter.size(), delimiter) == 0) {
            cursor += delimiter.size();
            const auto end = line.find(delimiter, cursor);
            if (end == std::string::npos) web_error(line_number, "unterminated 📜 text field");
            fields.push_back(line.substr(cursor, end - cursor));
            cursor = end + delimiter.size();
            if (cursor < line.size() &&
                !std::isspace(static_cast<unsigned char>(line[cursor])))
                web_error(line_number, "text field must be separated by whitespace");
            continue;
        }
        const auto start = cursor;
        while (cursor < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[cursor]))) ++cursor;
        fields.push_back(line.substr(start, cursor - start));
    }
    return fields;
}
bool valid_id(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!std::isalpha(first)) return false;
    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (!(std::isalnum(byte) || ch == '_' || ch == '-')) return false;
    }
    return true;
}

bool valid_language(std::string_view value) {
    if (value.size() < 2 || value.size() > 35) return false;
    for (const auto ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (!(std::isalnum(byte) || ch == '-')) return false;
    }
    return true;
}

bool has_scheme(std::string_view value, std::string_view scheme) {
    return value.size() >= scheme.size() &&
           std::equal(scheme.begin(), scheme.end(), value.begin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}
bool safe_root_relative_url(std::string_view value) {
    return value.size() >= 1 && value.front() == '/' &&
           (value.size() == 1 || value[1] != '/');
}

bool safe_link_url(std::string_view value) {
    if (value.empty() || value.size() > 4096) return false;
    return safe_root_relative_url(value) || value.front() == '#' ||
           has_scheme(value, "./") || has_scheme(value, "../") ||
           has_scheme(value, "https://") || has_scheme(value, "mailto:");
}

bool safe_image_url(std::string_view value) {
    if (value.empty() || value.size() > 4096) return false;
    return safe_root_relative_url(value) || has_scheme(value, "./") ||
           has_scheme(value, "../") || has_scheme(value, "https://");
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool safe_style_selector(std::string_view value) {
    if (value.empty() || value.size() > 128) return false;
    return value.find_first_of("<>{};@") == std::string_view::npos;
}
bool safe_style_property(std::string_view value) {
    static const std::unordered_set<std::string> allowed{
        "color", "background", "background-color", "font-family", "font-size",
        "font-weight", "font-style", "line-height", "text-align",
        "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
        "display", "gap", "row-gap", "column-gap", "grid-template-columns",
        "max-width", "min-width", "width", "height", "border", "border-color",
        "border-width", "border-style", "border-radius", "box-shadow",
        "align-items", "justify-content", "flex-direction", "flex-wrap",
        "list-style", "opacity"
    };
    return allowed.contains(lower_ascii(std::string(value)));
}

bool safe_style_value(std::string_view value) {
    if (value.empty() || value.size() > 256 ||
        value.find_first_of("<>{};") != std::string_view::npos) return false;
    const auto lower = lower_ascii(std::string(value));
    return lower.find("url(") == std::string::npos &&
           lower.find("@import") == std::string::npos &&
           lower.find("expression(") == std::string::npos;
}
bool allowed_child(BlockKind parent, WebNodeKind child) {
    switch (parent) {
        case BlockKind::Root:
        case BlockKind::Section:
            return child == WebNodeKind::Section || child == WebNodeKind::Navigation ||
                   child == WebNodeKind::Heading || child == WebNodeKind::Paragraph ||
                   child == WebNodeKind::Link || child == WebNodeKind::Image ||
                   child == WebNodeKind::Button || child == WebNodeKind::Form ||
                   child == WebNodeKind::List || child == WebNodeKind::Table;
        case BlockKind::Navigation:
            return child == WebNodeKind::Link;
        case BlockKind::Form:
            return child == WebNodeKind::Input || child == WebNodeKind::Paragraph;
        case BlockKind::List:
            return child == WebNodeKind::ListItem;
        case BlockKind::Table:
            return child == WebNodeKind::TableHeader || child == WebNodeKind::TableRow;
    }
    return false;
}

std::string node_id_attr(const WebNode& node) {
    return node.id.empty() ? std::string{} : " id=\"" + node.id + "\"";
}
std::string html_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (const char ch : value) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string json_escape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4) & 0xf]);
                    out.push_back(hex[ch & 0xf]);
                } else out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}
std::uint8_t parse_heading_level(const std::string& value, std::size_t line) {
    if (value.size() != 1 || value.front() < '1' || value.front() > '6')
        web_error(line, "heading level must be 1 through 6");
    return static_cast<std::uint8_t>(value.front() - '0');
}

std::string normalized_optional_id(const std::string& value, std::size_t line) {
    if (value == "-") return {};
    if (!valid_id(value)) web_error(line, "id must begin with ASCII letter and contain only letters, digits, _ or -");
    return value;
}

std::string input_type_from_token(const std::string& token, std::size_t line) {
    if (same_command(token, "🔤")) return "text";
    if (same_command(token, "🔢")) return "number";
    if (same_command(token, "📧")) return "email";
    web_error(line, "input type must be 🔤, 🔢, or 📧");
}

void require_arity(const std::vector<std::string>& fields, std::size_t expected,
                   std::size_t line, std::string_view syntax) {
    if (fields.size() != expected)
        web_error(line, std::string("syntax: ") + std::string(syntax));
}

void require_allowed(BlockKind parent, WebNodeKind child, std::size_t line) {
    if (!allowed_child(parent, child))
        web_error(line, web_node_kind_name(child) + " is not allowed in this block");
}
void collect_ids_and_verify_nodes(const std::vector<WebNode>& nodes,
                                  BlockKind parent,
                                  std::unordered_set<std::string>& ids,
                                  std::unordered_map<std::string, const WebNode*>& nodes_by_id,
                                  std::size_t depth,
                                  std::size_t& count) {
    if (depth > MaxWebDepth) throw std::runtime_error("web document nesting exceeds safety limit");
    for (const auto& node : nodes) {
        if (++count > MaxWebNodes) throw std::runtime_error("web document node count exceeds safety limit");
        if (!allowed_child(parent, node.kind))
            throw std::runtime_error("web document contains invalid parent/child relationship");
        if (!node.id.empty()) {
            if (!valid_id(node.id)) throw std::runtime_error("web document contains invalid id");
            if (!ids.insert(node.id).second)
                throw std::runtime_error("duplicate web element id '" + node.id + "'");
            nodes_by_id.emplace(node.id, &node);
        }

        switch (node.kind) {
            case WebNodeKind::Link:
                if (!safe_link_url(node.url)) throw std::runtime_error("web link URL is not allowed");
                break;
            case WebNodeKind::Image:
                if (!safe_image_url(node.url)) throw std::runtime_error("web image URL is not allowed");
                if (node.alt.size() > 1024) throw std::runtime_error("web image alt text exceeds limit");
                break;
            case WebNodeKind::Heading:
                if (node.heading_level < 1 || node.heading_level > 6)
                    throw std::runtime_error("web heading level is invalid");
                break;
            case WebNodeKind::Button:
                if (node.id.empty() || node.action.empty() || node.target.empty())
                    throw std::runtime_error("web button binding is incomplete");
                break;
            case WebNodeKind::Form: {
                if (node.id.empty() || node.action.empty() || node.target.empty())
                    throw std::runtime_error("web form binding is incomplete");
                std::size_t inputs = 0;
                for (const auto& child : node.children)
                    if (child.kind == WebNodeKind::Input) ++inputs;
                if (inputs == 0) throw std::runtime_error("web form must contain at least one input");
                break;
            }
            case WebNodeKind::Input:
                if (node.id.empty() || !valid_id(node.name))
                    throw std::runtime_error("web input requires valid id and name");
                if (node.input_type != "text" && node.input_type != "number" && node.input_type != "email")
                    throw std::runtime_error("web input type is invalid");
                break;
            case WebNodeKind::Navigation:
                if (node.children.empty()) throw std::runtime_error("web navigation must contain at least one link");
                break;
            case WebNodeKind::List:
                if (node.children.empty()) throw std::runtime_error("web list must contain at least one item");
                break;
            case WebNodeKind::Table: {
                if (node.children.size() < 2) throw std::runtime_error("web table requires header and data row");
                if (node.children.front().kind != WebNodeKind::TableHeader)
                    throw std::runtime_error("web table first row must be a header row");
                const auto width = node.children.front().cells.size();
                if (width == 0) throw std::runtime_error("web table header cannot be empty");
                std::size_t headers = 0;
                for (const auto& row : node.children) {
                    if (row.kind == WebNodeKind::TableHeader) ++headers;
                    if (row.cells.size() != width)
                        throw std::runtime_error("web table rows must have equal cell counts");
                }
                if (headers != 1) throw std::runtime_error("web table requires exactly one header row");
                break;
            }
            default:
                break;
        }
        BlockKind child_parent = parent;
        switch (node.kind) {
            case WebNodeKind::Section: child_parent = BlockKind::Section; break;
            case WebNodeKind::Navigation: child_parent = BlockKind::Navigation; break;
            case WebNodeKind::Form: child_parent = BlockKind::Form; break;
            case WebNodeKind::List: child_parent = BlockKind::List; break;
            case WebNodeKind::Table: child_parent = BlockKind::Table; break;
            default: break;
        }
        if (!node.children.empty())
            collect_ids_and_verify_nodes(node.children, child_parent, ids, nodes_by_id, depth + 1, count);
    }
}

std::string indent(std::size_t depth) {
    return std::string(depth * 2, ' ');
}

void render_node_html(const WebNode& node, std::ostringstream& out, std::size_t depth) {
    const auto pad = indent(depth);
    const auto id = node_id_attr(node);
    switch (node.kind) {
        case WebNodeKind::Section:
            out << pad << "<section" << id << ">\n";
            for (const auto& child : node.children) render_node_html(child, out, depth + 1);
            out << pad << "</section>\n";
            return;
        case WebNodeKind::Navigation:
            out << pad << "<nav" << id << " aria-label=\"Site navigation\">\n";
            for (const auto& child : node.children) render_node_html(child, out, depth + 1);
            out << pad << "</nav>\n";
            return;
        case WebNodeKind::Heading:
            out << pad << "<h" << static_cast<unsigned>(node.heading_level) << id << ">"
                << html_escape(node.text) << "</h" << static_cast<unsigned>(node.heading_level) << ">\n";
            return;
        case WebNodeKind::Paragraph:
            out << pad << "<p" << id << ">" << html_escape(node.text) << "</p>\n";
            return;
        case WebNodeKind::Link:
            out << pad << "<a" << id << " href=\"" << html_escape(node.url) << "\">"
                << html_escape(node.text) << "</a>\n";
            return;
        case WebNodeKind::Image:
            out << pad << "<img" << id << " src=\"" << html_escape(node.url)
                << "\" alt=\"" << html_escape(node.alt) << "\">\n";
            return;
        case WebNodeKind::Button:
            out << pad << "<button type=\"button\"" << id
                << " data-emojineer-event=\"click\" data-emojineer-export=\""
                << html_escape(node.action) << "\" data-emojineer-target=\""
                << html_escape(node.target) << "\">" << html_escape(node.text) << "</button>\n";
            return;
        case WebNodeKind::Form:
            out << pad << "<form" << id
                << " data-emojineer-event=\"submit\" data-emojineer-export=\""
                << html_escape(node.action) << "\" data-emojineer-target=\""
                << html_escape(node.target) << "\">\n";
            for (const auto& child : node.children) render_node_html(child, out, depth + 1);
            out << indent(depth + 1) << "<button type=\"submit\">"
                << html_escape(node.text) << "</button>\n";
            out << pad << "</form>\n";
            return;
        case WebNodeKind::Input:
            out << pad << "<label for=\"" << html_escape(node.id) << "\">"
                << html_escape(node.text) << "</label>\n";
            out << pad << "<input id=\"" << html_escape(node.id) << "\" name=\""
                << html_escape(node.name) << "\" type=\"" << html_escape(node.input_type) << "\">\n";
            return;
        case WebNodeKind::List:
            out << pad << (node.ordered ? "<ol" : "<ul") << id << ">\n";
            for (const auto& child : node.children) render_node_html(child, out, depth + 1);
            out << pad << (node.ordered ? "</ol>\n" : "</ul>\n");
            return;
        case WebNodeKind::ListItem:
            out << pad << "<li>" << html_escape(node.text) << "</li>\n";
            return;
        case WebNodeKind::Table:
            out << pad << "<table" << id << ">\n";
            for (const auto& child : node.children) render_node_html(child, out, depth + 1);
            out << pad << "</table>\n";
            return;
        case WebNodeKind::TableHeader:
            out << pad << "<thead><tr>";
            for (const auto& cell : node.cells)
                out << "<th scope=\"col\">" << html_escape(cell) << "</th>";
            out << "</tr></thead>\n";
            return;
        case WebNodeKind::TableRow:
            out << pad << "<tbody><tr>";
            for (const auto& cell : node.cells)
                out << "<td>" << html_escape(cell) << "</td>";
            out << "</tr></tbody>\n";
            return;
    }
}
void render_node_ir(const WebNode& node, std::ostringstream& out) {
    out << "{\"kind\":\"" << json_escape(web_node_kind_name(node.kind)) << "\"";
    if (!node.id.empty()) out << ",\"id\":\"" << json_escape(node.id) << "\"";
    if (!node.text.empty()) out << ",\"text\":\"" << json_escape(node.text) << "\"";
    if (!node.url.empty()) out << ",\"url\":\"" << json_escape(node.url) << "\"";
    if (!node.alt.empty()) out << ",\"alt\":\"" << json_escape(node.alt) << "\"";
    if (!node.name.empty()) out << ",\"name\":\"" << json_escape(node.name) << "\"";
    if (!node.input_type.empty()) out << ",\"inputType\":\"" << json_escape(node.input_type) << "\"";
    if (!node.action.empty()) out << ",\"action\":\"" << json_escape(node.action) << "\"";
    if (!node.target.empty()) out << ",\"target\":\"" << json_escape(node.target) << "\"";
    if (node.heading_level) out << ",\"level\":" << static_cast<unsigned>(node.heading_level);
    if (node.kind == WebNodeKind::List) out << ",\"ordered\":" << (node.ordered ? "true" : "false");
    if (!node.cells.empty()) {
        out << ",\"cells\":[";
        for (std::size_t i = 0; i < node.cells.size(); ++i) {
            if (i) out << ',';
            out << "\"" << json_escape(node.cells[i]) << "\"";
        }
        out << ']';
    }
    if (!node.children.empty()) {
        out << ",\"children\":[";
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            if (i) out << ',';
            render_node_ir(node.children[i], out);
        }
        out << ']';
    }
    out << '}';
}
} // namespace

std::string web_node_kind_name(WebNodeKind kind) {
    switch (kind) {
        case WebNodeKind::Section: return "section";
        case WebNodeKind::Navigation: return "navigation";
        case WebNodeKind::Heading: return "heading";
        case WebNodeKind::Paragraph: return "paragraph";
        case WebNodeKind::Link: return "link";
        case WebNodeKind::Image: return "image";
        case WebNodeKind::Button: return "button";
        case WebNodeKind::Form: return "form";
        case WebNodeKind::Input: return "input";
        case WebNodeKind::List: return "list";
        case WebNodeKind::ListItem: return "list-item";
        case WebNodeKind::Table: return "table";
        case WebNodeKind::TableHeader: return "table-header";
        case WebNodeKind::TableRow: return "table-row";
    }
    throw std::runtime_error("unknown web node kind");
}

std::string web_event_kind_name(WebEventKind kind) {
    switch (kind) {
        case WebEventKind::Click: return "click";
        case WebEventKind::Submit: return "submit";
    }
    throw std::runtime_error("unknown web event kind");
}
WebDocument parse_web_document(std::string_view source) {
    if (source.size() > MaxWebSourceBytes)
        throw std::runtime_error("web source exceeds 512 KiB safety limit");

    WebDocument document;
    std::vector<Frame> frames{{&document.children, BlockKind::Root}};
    bool have_document = false;
    std::size_t node_count = 0;
    std::istringstream input{std::string(source)};
    std::string raw;
    std::size_t line_number = 0;

    auto add_node = [&](WebNode node, BlockKind opened = BlockKind::Root, bool opens = false) {
        require_allowed(frames.back().kind, node.kind, line_number);
        if (node_count >= MaxWebNodes) web_error(line_number, "web node count exceeds safety limit");
        ++node_count;
        frames.back().children->push_back(std::move(node));
        if (opens) {
            if (frames.size() >= MaxWebDepth)
                web_error(line_number, "web nesting exceeds safety limit");
            auto& inserted = frames.back().children->back();
            frames.push_back({&inserted.children, opened});
        }
    };

    while (std::getline(input, raw)) {
        ++line_number;
        const auto line = trim(raw);
        if (line.empty()) continue;
        const auto fields = web_fields(line, line_number);
        if (fields.empty()) continue;
        if (same_command(fields.front(), "💭")) continue;

        if (!have_document) {
            if (!same_command(fields.front(), "🌐"))
                web_error(line_number, "first statement must be 🌐 document declaration");
            if (fields.size() != 2 && fields.size() != 3)
                web_error(line_number, "syntax: 🌐 📜title📜 [📜language📜]");
            if (fields[1].empty() || fields[1].size() > 512)
                web_error(line_number, "document title must contain 1..512 bytes");
            document.title = fields[1];
            if (fields.size() == 3) {
                if (!valid_language(fields[2])) web_error(line_number, "invalid document language tag");
                document.language = fields[2];
            }
            have_document = true;
            continue;
        }
        if (same_command(fields.front(), "🌐"))
            web_error(line_number, "document declaration may appear only once");

        if (same_command(fields.front(), "🏁")) {
            require_arity(fields, 1, line_number, "🏁");
            if (frames.size() == 1) web_error(line_number, "🏁 has no open web block");
            frames.pop_back();
            continue;
        }

        if (same_command(fields.front(), "🎨")) {
            require_arity(fields, 4, line_number, "🎨 📜selector📜 📜property📜 📜value📜");
            if (frames.size() != 1) web_error(line_number, "🎨 style rules are document-level only");
            if (!safe_style_selector(fields[1])) web_error(line_number, "unsafe style selector");
            if (!safe_style_property(fields[2])) web_error(line_number, "style property is not in the safe allowlist");
            if (!safe_style_value(fields[3])) web_error(line_number, "unsafe style value");
            document.styles.push_back({fields[1], lower_ascii(fields[2]), fields[3]});
            continue;
        }

        if (same_command(fields.front(), "📑")) {
            require_arity(fields, 2, line_number, "📑 <id>");
            WebNode node; node.kind = WebNodeKind::Section;
            node.id = normalized_optional_id(fields[1], line_number);
            add_node(std::move(node), BlockKind::Section, true);
            continue;
        }

        if (same_command(fields.front(), "🧭")) {
            require_arity(fields, 2, line_number, "🧭 <id>");
            WebNode node; node.kind = WebNodeKind::Navigation;
            node.id = normalized_optional_id(fields[1], line_number);
            add_node(std::move(node), BlockKind::Navigation, true);
            continue;
        }
        if (same_command(fields.front(), "🔠")) {
            require_arity(fields, 4, line_number, "🔠 <1..6> <id|-> 📜text📜");
            WebNode node; node.kind = WebNodeKind::Heading;
            node.heading_level = parse_heading_level(fields[1], line_number);
            node.id = normalized_optional_id(fields[2], line_number);
            node.text = fields[3];
            add_node(std::move(node));
            continue;
        }

        if (same_command(fields.front(), "🧾")) {
            require_arity(fields, 3, line_number, "🧾 <id|-> 📜text📜");
            WebNode node; node.kind = WebNodeKind::Paragraph;
            node.id = normalized_optional_id(fields[1], line_number);
            node.text = fields[2];
            add_node(std::move(node));
            continue;
        }

        if (same_command(fields.front(), "🔗")) {
            require_arity(fields, 4, line_number, "🔗 <id|-> 📜url📜 📜text📜");
            if (!safe_link_url(fields[2])) web_error(line_number, "link URL is not allowed");
            WebNode node; node.kind = WebNodeKind::Link;
            node.id = normalized_optional_id(fields[1], line_number);
            node.url = fields[2]; node.text = fields[3];
            add_node(std::move(node));
            continue;
        }

        if (same_command(fields.front(), "🖼️")) {
            require_arity(fields, 4, line_number, "🖼️ <id|-> 📜url📜 📜alt text📜");
            if (!safe_image_url(fields[2])) web_error(line_number, "image URL is not allowed");
            WebNode node; node.kind = WebNodeKind::Image;
            node.id = normalized_optional_id(fields[1], line_number);
            node.url = fields[2]; node.alt = fields[3];
            add_node(std::move(node));
            continue;
        }
        if (same_command(fields.front(), "🔘")) {
            require_arity(fields, 5, line_number,
                          "🔘 <id> 📜label📜 <export.name> <target-id>");
            if (!valid_id(fields[1])) web_error(line_number, "button requires a valid id");
            validate_interop_external_name(fields[3]);
            if (!valid_id(fields[4])) web_error(line_number, "button target must be a valid id");
            WebNode node; node.kind = WebNodeKind::Button;
            node.id = fields[1]; node.text = fields[2];
            node.action = fields[3]; node.target = fields[4];
            document.actions.push_back({node.id, WebEventKind::Click, node.action, node.target});
            add_node(std::move(node));
            continue;
        }

        if (same_command(fields.front(), "📝")) {
            require_arity(fields, 5, line_number,
                          "📝 <id> <export.name> <target-id> 📜submit label📜");
            if (!valid_id(fields[1])) web_error(line_number, "form requires a valid id");
            validate_interop_external_name(fields[2]);
            if (!valid_id(fields[3])) web_error(line_number, "form target must be a valid id");
            WebNode node; node.kind = WebNodeKind::Form;
            node.id = fields[1]; node.action = fields[2];
            node.target = fields[3]; node.text = fields[4];
            document.actions.push_back({node.id, WebEventKind::Submit, node.action, node.target});
            add_node(std::move(node), BlockKind::Form, true);
            continue;
        }

        if (same_command(fields.front(), "🏷️")) {
            require_arity(fields, 5, line_number,
                          "🏷️ <id> <name> <🔤|🔢|📧> 📜label📜");
            if (!valid_id(fields[1]) || !valid_id(fields[2]))
                web_error(line_number, "input id and name must be valid identifiers");
            WebNode node; node.kind = WebNodeKind::Input;
            node.id = fields[1]; node.name = fields[2];
            node.input_type = input_type_from_token(fields[3], line_number);
            node.text = fields[4];
            add_node(std::move(node));
            continue;
        }
        if (same_command(fields.front(), "📋")) {
            require_arity(fields, 3, line_number, "📋 <id|-> <🔢|🔹>");
            WebNode node; node.kind = WebNodeKind::List;
            node.id = normalized_optional_id(fields[1], line_number);
            if (same_command(fields[2], "🔢")) node.ordered = true;
            else if (same_command(fields[2], "🔹")) node.ordered = false;
            else web_error(line_number, "list mode must be 🔢 ordered or 🔹 unordered");
            add_node(std::move(node), BlockKind::List, true);
            continue;
        }

        if (same_command(fields.front(), "🔹")) {
            require_arity(fields, 2, line_number, "🔹 📜item text📜");
            WebNode node; node.kind = WebNodeKind::ListItem; node.text = fields[1];
            add_node(std::move(node));
            continue;
        }

        if (same_command(fields.front(), "📊")) {
            require_arity(fields, 2, line_number, "📊 <id|->");
            WebNode node; node.kind = WebNodeKind::Table;
            node.id = normalized_optional_id(fields[1], line_number);
            add_node(std::move(node), BlockKind::Table, true);
            continue;
        }

        if (same_command(fields.front(), "📌") || same_command(fields.front(), "📍")) {
            if (fields.size() < 2) web_error(line_number, "table row requires at least one cell");
            WebNode node;
            node.kind = same_command(fields.front(), "📌")
                            ? WebNodeKind::TableHeader : WebNodeKind::TableRow;
            node.cells.assign(fields.begin() + 1, fields.end());
            add_node(std::move(node));
            continue;
        }

        web_error(line_number, "unknown web markup command '" + fields.front() + "'");
    }
    if (!have_document) throw std::runtime_error("web source is missing 🌐 document declaration");
    if (frames.size() != 1) throw std::runtime_error("web source ended with unclosed block(s)");
    verify_web_document(document);
    return document;
}

void verify_web_document(const WebDocument& document) {
    if (document.title.empty() || document.title.size() > 512)
        throw std::runtime_error("web document title is invalid");
    if (!valid_language(document.language))
        throw std::runtime_error("web document language is invalid");
    if (document.children.empty())
        throw std::runtime_error("web document must contain at least one semantic element");

    for (const auto& style : document.styles) {
        if (!safe_style_selector(style.selector) ||
            !safe_style_property(style.property) ||
            !safe_style_value(style.value))
            throw std::runtime_error("web document contains unsafe style rule");
    }

    std::unordered_set<std::string> ids;
    std::unordered_map<std::string, const WebNode*> nodes_by_id;
    std::size_t count = 0;
    collect_ids_and_verify_nodes(document.children, BlockKind::Root, ids, nodes_by_id, 1, count);

    std::unordered_set<std::string> bound_elements;
    for (const auto& action : document.actions) {
        if (!valid_id(action.element_id) || !ids.contains(action.element_id))
            throw std::runtime_error("web action references missing element '" + action.element_id + "'");
        if (!valid_id(action.target_id) || !ids.contains(action.target_id))
            throw std::runtime_error("web action references missing target '" + action.target_id + "'");
        validate_interop_external_name(action.export_name);
        const auto* element = nodes_by_id.at(action.element_id);
        if (action.event == WebEventKind::Click && element->kind != WebNodeKind::Button)
            throw std::runtime_error("web click action must bind a button element");
        if (action.event == WebEventKind::Submit && element->kind != WebNodeKind::Form)
            throw std::runtime_error("web submit action must bind a form element");
        if (element->action != action.export_name || element->target != action.target_id)
            throw std::runtime_error("web action binding does not match its element contract");
        if (!bound_elements.insert(action.element_id).second)
            throw std::runtime_error("web element has multiple action bindings");
    }
}
std::string render_web_document_html(const WebDocument& document) {
    verify_web_document(document);
    std::ostringstream out;
    out << "<!doctype html>\n"
        << "<html lang=\"" << html_escape(document.language) << "\">\n"
        << "<head>\n"
        << "  <meta charset=\"utf-8\">\n"
        << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        << "  <title>" << html_escape(document.title) << "</title>\n";
    if (!document.styles.empty()) {
        out << "  <style>\n";
        for (const auto& style : document.styles)
            out << "    " << style.selector << " { " << style.property << ": "
                << style.value << "; }\n";
        out << "  </style>\n";
    }
    out << "</head>\n<body>\n<main>\n";
    for (const auto& node : document.children) render_node_html(node, out, 1);
    out << "</main>\n</body>\n</html>\n";
    return out.str();
}

std::string render_web_document_ir(const WebDocument& document) {
    verify_web_document(document);
    std::ostringstream out;
    out << "{\"schema\":\"emojineer.web-ir.v1\","
        << "\"title\":\"" << json_escape(document.title) << "\","
        << "\"language\":\"" << json_escape(document.language) << "\","
        << "\"styles\":[";
    for (std::size_t i = 0; i < document.styles.size(); ++i) {
        if (i) out << ',';
        const auto& style = document.styles[i];
        out << "{\"selector\":\"" << json_escape(style.selector)
            << "\",\"property\":\"" << json_escape(style.property)
            << "\",\"value\":\"" << json_escape(style.value) << "\"}";
    }
    out << "],\"children\":[";
    for (std::size_t i = 0; i < document.children.size(); ++i) {
        if (i) out << ',';
        render_node_ir(document.children[i], out);
    }
    out << "],\"actions\":" << render_web_bindings_json(document) << "}\n";
    return out.str();
}
std::string render_web_bindings_json(const WebDocument& document) {
    verify_web_document(document);
    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < document.actions.size(); ++i) {
        if (i) out << ',';
        const auto& action = document.actions[i];
        out << "{\"elementId\":\"" << json_escape(action.element_id)
            << "\",\"event\":\"" << json_escape(web_event_kind_name(action.event))
            << "\",\"export\":\"" << json_escape(action.export_name)
            << "\",\"targetId\":\"" << json_escape(action.target_id) << "\"}";
    }
    out << ']';
    return out.str();
}

WebBehaviorResult invoke_web_behavior(std::string_view behavior_source,
                                      std::string_view export_name,
                                      std::string_view payload) {
    WebBehaviorResult result;
    if (behavior_source.size() > MaxBehaviorSourceBytes) {
        result.diagnostic = "web behavior source exceeds 256 KiB safety limit";
        return result;
    }
    if (payload.size() > MaxBehaviorPayloadBytes) {
        result.diagnostic = "web behavior payload exceeds 64 KiB safety limit";
        return result;
    }

    try {
        CustomEmojiRegistry registry;
        Lexer lexer(std::string(behavior_source), registry);
        Parser parser(lexer.tokenize());
        Compiler compiler;
        auto chunk = compiler.compile(parser.parse());
        result.required_capabilities = chunk.required_capabilities;

        const InteropExportInfo* export_info = nullptr;
        for (const auto& candidate : chunk.interop_exports) {
            if (candidate.external_name == export_name) {
                export_info = &candidate;
                break;
            }
        }
        if (!export_info)
            throw std::runtime_error("web behavior export '" + std::string(export_name) + "' is not declared");
        if (export_info->signature.parameters.size() != 1 ||
            export_info->signature.parameters.front() != InteropType::String ||
            export_info->signature.result != InteropType::String)
            throw std::runtime_error("web behavior export must have signature (text) -> text");

        std::istringstream input;
        std::ostringstream output;
        ExecutionPolicy policy;
        policy.mode = ExecutionMode::Sandbox;
        VM vm(input, output, 1'000'000, policy);
        auto value = vm.invoke_export(chunk, export_name, {std::string(payload)});
        const auto* text = std::get_if<std::string>(&value);
        if (!text) throw std::runtime_error("web behavior export returned non-text value");
        if (text->size() > MaxBehaviorResponseBytes)
            throw std::runtime_error("web behavior response exceeds 1 MiB safety limit");
        result.response_text = *text;
        result.ok = true;
    } catch (const std::exception& error) {
        result.diagnostic = error.what();
    }
    return result;
}

} // namespace emojineer
