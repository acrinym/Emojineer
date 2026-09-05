from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)

hpp = Path('include/emojineer/debugger.hpp')
text = hpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    std::vector<BreakpointLocation> get_breakpoints() const;\n    void rebuild_breakpoint_index();',
    '    std::vector<BreakpointLocation> get_breakpoints() const;\n    std::vector<BreakpointInfo> get_breakpoint_info() const;\n    void rebuild_breakpoint_index();',
    'DebugVM breakpoint info API')
hpp.write_text(text, encoding='utf-8')

cpp = Path('src/debugger.cpp')
text = cpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    'std::vector<BreakpointLocation> DebugVM::get_breakpoints() const {\n    return controller_->get_breakpoints();\n}\n\nvoid DebugVM::rebuild_breakpoint_index() {',
    'std::vector<BreakpointLocation> DebugVM::get_breakpoints() const {\n    return controller_->get_breakpoints();\n}\n\nstd::vector<BreakpointInfo> DebugVM::get_breakpoint_info() const {\n    return controller_->get_breakpoint_info();\n}\n\nvoid DebugVM::rebuild_breakpoint_index() {',
    'DebugVM breakpoint info implementation')
cpp.write_text(text, encoding='utf-8')

tests = Path('tests/debugger_tests.cpp')
text = tests.read_text(encoding='utf-8')
start = text.index('void test_breakpoint_binding_diagnostics() {')
end = text.index('// Test: Step over with inner breakpoint', start)
replacement = r'''void test_breakpoint_binding_diagnostics() {
    std::cout << "Test: breakpoint binding diagnostics...\n";
    const std::string source = "📝 📜hello📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);

    emojineer::BreakpointLocation valid;
    valid.source_position.source_path = "test.emoji";
    valid.source_position.line = 1;
    valid.enabled = true;
    vm.add_breakpoint(valid);

    emojineer::BreakpointLocation invalid;
    invalid.source_position.source_path = "test.emoji";
    invalid.source_position.line = 100;
    invalid.enabled = true;
    vm.add_breakpoint(invalid);

    // Load the chunk without executing it so binding can be inspected before side effects.
    vm.execute(chunk);
    auto infos = vm.get_breakpoint_info();
    require(infos.size() == 2, "expected two breakpoint diagnostics");
    require(infos[0].status == emojineer::BreakpointStatus::Bound,
            "line 1 breakpoint must be Bound");
    require(infos[0].bound_ip.has_value(), "Bound breakpoint must expose an IP");
    require(infos[0].diagnostics.find("Bound to IP") != std::string::npos,
            "Bound breakpoint diagnostic must name its IP");
    require(infos[1].status == emojineer::BreakpointStatus::Unbound,
            "nonexistent line must be Unbound when no source resolver proves drift");
    require(!infos[1].bound_ip.has_value(), "Unbound breakpoint must not expose an IP");

    // Remove the real breakpoint. The remaining unbound breakpoint must never stop execution.
    vm.remove_breakpoint(0);
    vm.continue_run();
    vm.execute(chunk);
    require(vm.is_finished(), "unbound breakpoint must not stop program completion");
    require(output.str() == "hello\n", "program behavior must remain unchanged by an unbound breakpoint");
    std::cout << "  ✅ Breakpoint binding diagnostics work\n";
}

'''
text = text[:start] + replacement + text[end:]
tests.write_text(text, encoding='utf-8')
print('Train 18 breakpoint binding diagnostics repaired and asserted.')
