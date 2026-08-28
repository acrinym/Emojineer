from pathlib import Path

p = Path("tests/lsp_tests.cpp")
text = p.read_text()
old = '    std::string params = R"({\\"processId\\":)" + std::to_string(getpid()) + R(,\\"rootUri\\":\\"file:///test\\",\\"capabilities\\":{}})";'
new = '    std::string params = "{\\"processId\\":" + std::to_string(getpid()) + ",\\"rootUri\\":\\"file:///test\\",\\"capabilities\\":{}}";'
if old in text:
    if text.count(old) != 1:
        raise SystemExit("initialize JSON fixture: expected one malformed construction")
    p.write_text(text.replace(old, new, 1))
    print("applied: initialize JSON fixture construction")
elif new in text:
    print("already applied: initialize JSON fixture construction")
else:
    raise SystemExit("initialize JSON fixture: expected construction not found")
