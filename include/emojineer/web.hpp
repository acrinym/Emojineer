#pragma once

#include "emojineer/capability.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace emojineer {

enum class WebNodeKind {
    Section,
    Navigation,
    Heading,
    Paragraph,
    Link,
    Image,
    Button,
    Form,
    Input,
    List,
    ListItem,
    Table,
    TableHeader,
    TableRow,
};

enum class WebEventKind {
    Click,
    Submit,
};

struct WebStyleRule {
    std::string selector;
    std::string property;
    std::string value;
};
struct WebActionBinding {
    std::string element_id;
    WebEventKind event{WebEventKind::Click};
    std::string export_name;
    std::string target_id;
};

struct WebNode {
    WebNodeKind kind{WebNodeKind::Paragraph};
    std::string id;
    std::string text;
    std::string url;
    std::string alt;
    std::string name;
    std::string input_type;
    std::string action;
    std::string target;
    std::uint8_t heading_level{0};
    bool ordered{false};
    std::vector<std::string> cells;
    std::vector<WebNode> children;
};

struct WebDocument {
    std::string title;
    std::string language{"en"};
    std::vector<WebStyleRule> styles;
    std::vector<WebNode> children;
    std::vector<WebActionBinding> actions;
};
struct WebBehaviorResult {
    bool ok{false};
    std::string response_text;
    std::string diagnostic;
    CapabilityMask required_capabilities{0};
};

std::string web_node_kind_name(WebNodeKind kind);
std::string web_event_kind_name(WebEventKind kind);

WebDocument parse_web_document(std::string_view source);
void verify_web_document(const WebDocument& document);

std::string render_web_document_html(const WebDocument& document);
std::string render_web_document_ir(const WebDocument& document);
std::string render_web_bindings_json(const WebDocument& document);

WebBehaviorResult invoke_web_behavior(std::string_view behavior_source,
                                      std::string_view export_name,
                                      std::string_view payload);

} // namespace emojineer
