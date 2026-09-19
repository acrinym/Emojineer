#include "emojineer/web.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void expect_error(const std::function<void()>& action, const std::string& needle) {
    try {
        action();
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(needle) != std::string::npos) return;
        throw std::runtime_error("wrong error: " + std::string(error.what()));
    }
    throw std::runtime_error("expected error containing: " + needle);
}

const char* page_source = R"WEB(🌐 📜Demo <&>📜 📜en-US📜
🎨 📜body📜 📜color📜 📜#222📜
📑 main
🔠 1 hero 📜Hello <world>📜
🧾 status 📜Ready & waiting📜
🧭 nav
🔗 home 📜/📜 📜Home📜
🏁
🖼️ logo 📜/logo.png📜 📜Logo "mark"📜
🔘 go 📜Go📜 web.greet status
📋 items 🔹
🔹 📜One📜
🔹 📜Two📜
🏁
📊 data
📌 📜Name📜 📜Age📜
📍 📜Ada📜 📜37📜
🏁
📝 contact web.submit status 📜Send📜
🏷️ email email 📧 📜Email address📜
🏁
🏁
)WEB";
void test_complete_document_and_renderers() {
    const auto document = emojineer::parse_web_document(page_source);
    require(document.title == "Demo <&>", "document title should parse");
    require(document.language == "en-US", "language should parse");
    require(document.actions.size() == 2, "button and form should create bindings");

    const auto html = emojineer::render_web_document_html(document);
    require(html.find("<h1 id=\"hero\">Hello &lt;world&gt;</h1>") != std::string::npos,
            "heading should be semantic and escaped");
    require(html.find("<nav id=\"nav\" aria-label=\"Site navigation\">") != std::string::npos,
            "navigation should be semantic and labelled");
    require(html.find("alt=\"Logo &quot;mark&quot;\"") != std::string::npos,
            "image alt text should be escaped");
    require(html.find("<label for=\"email\">Email address</label>") != std::string::npos,
            "form input should have an explicit label");
    require(html.find("<th scope=\"col\">Name</th>") != std::string::npos,
            "table headers should use scoped th cells");
    require(html.find("<script") == std::string::npos,
            "generated markup must not inject a script language");

    const auto ir = emojineer::render_web_document_ir(document);
    require(ir.find("emojineer.web-ir.v1") != std::string::npos,
            "IR should identify its schema");
    const auto bindings = emojineer::render_web_bindings_json(document);
    require(bindings.find("web.greet") != std::string::npos &&
            bindings.find("web.submit") != std::string::npos,
            "bindings should expose explicit Emojineer exports");
}
void test_hostile_markup_is_rejected() {
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🔗 bad 📜javascript:alert(1)📜 📜bad📜\n");
    }, "link URL");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🔗 bad 📜//evil.example/path📜 📜bad📜\n");
    }, "link URL");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🖼️ bad 📜//evil.example/pixel.png📜 📜tracking pixel📜\n");
    }, "image URL");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🎨 📜body📜 📜background📜 📜url(https://evil.test/x)📜\n"
            "🧾 ok 📜x📜\n");
    }, "unsafe style value");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🧾 same 📜one📜\n🧾 same 📜two📜\n");
    }, "duplicate web element id");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n🧾 status 📜x📜\n🔘 go 📜Go📜 web.go missing\n");
    }, "missing target");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n📊 t\n📌 📜A📜 📜B📜\n📍 📜1📜\n🏁\n");
    }, "equal cell counts");
    expect_error([] {
        (void)emojineer::parse_web_document(
            "🌐 📜x📜\n📑 main\n🧾 p 📜unterminated block📜\n");
    }, "unclosed block");
}
void test_behavior_uses_production_vm_and_sandbox() {
    const std::string echo = R"EMJ(📡 🚀 📜web.echo📜 🔤 🫴 🔤 🤲
🛠️ 🚀 🫴 🍎 🤲
📦 📜echo: 📜 ➕ 🍎
🏁
)EMJ";
    const auto success = emojineer::invoke_web_behavior(echo, "web.echo", "hello");
    require(success.ok, "pure web behavior should execute");
    require(success.response_text == "echo: hello", "behavior should return export result");
    require(success.required_capabilities == 0, "pure behavior should require no capability");

    const std::string network = R"EMJ(📡 🚀 📜web.fetch📜 🔤 🫴 🔤 🤲
🛠️ 🚀 🫴 🍎 🤲
📦 🌐 🫴 🍎 🤲
🏁
)EMJ";
    const auto denied = emojineer::invoke_web_behavior(network, "web.fetch", "https://example.com");
    require(!denied.ok, "browser behavior must deny native host authority");
    require(denied.required_capabilities ==
                emojineer::capability_mask(emojineer::Capability::Network),
            "denied behavior should report its network requirement");
    require(denied.response_text.empty(), "denial should happen before response effects");

    const auto missing = emojineer::invoke_web_behavior(echo, "web.missing", "x");
    require(!missing.ok && missing.diagnostic.find("not declared") != std::string::npos,
            "undeclared behavior export should be rejected");
}
void test_bindings_must_match_semantic_element_kind() {
    emojineer::WebDocument document;
    document.title = "binding";
    emojineer::WebNode trigger;
    trigger.kind = emojineer::WebNodeKind::Paragraph;
    trigger.id = "trigger";
    trigger.text = "not a button";
    emojineer::WebNode target;
    target.kind = emojineer::WebNodeKind::Paragraph;
    target.id = "target";
    target.text = "target";
    document.children = {trigger, target};
    document.actions.push_back(
        {"trigger", emojineer::WebEventKind::Click, "web.go", "target"});
    expect_error([&] { emojineer::verify_web_document(document); }, "click action");
}

} // namespace

int main() {
    try {
        test_complete_document_and_renderers();
        test_hostile_markup_is_rejected();
        test_behavior_uses_production_vm_and_sandbox();
        test_bindings_must_match_semantic_element_kind();
        std::cout << "web tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "web tests failed: " << error.what() << '\n';
        return 1;
    }
}
