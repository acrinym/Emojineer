from pathlib import Path

path = Path('tests/debugger_tests.cpp')
text = path.read_text(encoding='utf-8')

start = text.index('void test_evaluate_with_params_locals() {')
end = text.index('// Test: Frame selection', start)
region = text[start:end]
region = region.replace(
    'require(std::holds_alternative<std::int64_t>(*param1_val), "🍎 should be an integer");',
    'require(std::holds_alternative<double>(*param1_val), "🍎 should preserve the runtime number representation");')
region = region.replace(
    'require(std::holds_alternative<std::int64_t>(*param2_val), "🍐 should be an integer");',
    'require(std::holds_alternative<double>(*param2_val), "🍐 should preserve the runtime number representation");')
region = region.replace(
    'require(std::holds_alternative<std::int64_t>(*local_val), "🍇 should be an integer");',
    'require(std::holds_alternative<double>(*local_val), "🍇 should preserve the runtime number representation");')
region = region.replace('auto param1_int = std::get<std::int64_t>(*param1_val);', 'auto param1_number = std::get<double>(*param1_val);')
region = region.replace('auto param2_int = std::get<std::int64_t>(*param2_val);', 'auto param2_number = std::get<double>(*param2_val);')
region = region.replace('auto local_int = std::get<std::int64_t>(*local_val);', 'auto local_number = std::get<double>(*local_val);')
region = region.replace('require(param1_int == 10, "🍎 should equal 10");', 'require(param1_number == 10.0, "🍎 should equal 10");')
region = region.replace('require(param2_int == 20, "🍐 should equal 20");', 'require(param2_number == 20.0, "🍐 should equal 20");')
region = region.replace('require(local_int == 30, "🍇 should equal 30 (🍎 + 🍐)");', 'require(local_number == 30.0, "🍇 should equal 30 (🍎 + 🍐)");')
region = region.replace('<< param1_int <<', '<< param1_number <<')
region = region.replace('<< param2_int <<', '<< param2_number <<')
region = region.replace('<< local_int <<', '<< local_number <<')
text = text[:start] + region + text[end:]

start = text.index('void test_frame_selection() {')
end = text.index('// Test: Breakpoint doesn\'t immediately re-hit', start)
replacement = r'''void test_frame_selection() {
    std::cout << "Test: nested frame selection...\n";
    const std::string source =
        "🛠️ ⭐ 🫴 🍎 🤲\n"
        "📝 🍎\n"
        "📦 🍎\n"
        "🏁\n"
        "🛠️ 🌟 🫴 🍐 🤲\n"
        "📦 ⭐ 🫴 🍐 🤲\n"
        "🏁\n"
        "📝 🌟 🫴 📜hi📜 🤲\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 2;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "nested breakpoint should produce a snapshot");
    require(snapshot->current_position.line == 2, "should pause inside ⭐");
    require(snapshot->call_stack.size() == 2, "nested call should expose exactly two function frames");
    require(snapshot->call_stack[0].function_name == "⭐", "frame 0 must be innermost ⭐");
    require(snapshot->call_stack[1].function_name == "🌟", "frame 1 must be caller 🌟");
    require(vm.select_frame(0), "should select innermost frame 0");
    require(vm.selected_frame() == 0, "selected frame should be 0");
    require(vm.select_frame(1), "should select caller frame 1");
    require(vm.selected_frame() == 1, "selected frame should be 1");
    require(!vm.select_frame(99), "out-of-range frame selection must fail");
    require(vm.selected_frame() == 1, "failed selection must preserve the prior selected frame");
    std::cout << "  ✅ Nested frame selection works\n";
}

'''
text = text[:start] + replacement + text[end:]

path.write_text(text, encoding='utf-8')
print('Train 18 debugger numeric and nested-frame acceptance aligned with production semantics.')
