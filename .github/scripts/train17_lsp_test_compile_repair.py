from pathlib import Path

p = Path("tests/lsp_tests.cpp")
lines = p.read_text().splitlines()
replacement = '    std::string params = "{\\"processId\\":" + std::to_string(getpid()) + ",\\"rootUri\\":\\"file:///test\\",\\"capabilities\\":{}}";'
matches = [i for i, line in enumerate(lines) if "std::string params =" in line and "processId" in line and "rootUri" in line]
if len(matches) != 1:
    raise SystemExit(f"initialize JSON fixture: expected one processId/rootUri construction, got {len(matches)}")
index = matches[0]
if lines[index] == replacement:
    print("already applied: initialize JSON fixture construction")
else:
    lines[index] = replacement
    p.write_text("\n".join(lines) + "\n")
    print("applied: initialize JSON fixture construction")
