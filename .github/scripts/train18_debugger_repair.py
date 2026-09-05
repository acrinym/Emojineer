from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# Make the pre-execution hook stateful so it can bind a newly initialized chunk before
# the first executable instruction and record a real breakpoint pause.
types = Path('include/emojineer/debug_types.hpp')
text = types.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    virtual bool should_pause_before_execution() const = 0;',
    '    virtual bool should_pause_before_execution() = 0;',
    'mutable pre-execution debugger hook')
types.write_text(text, encoding='utf-8')

hpp = Path('include/emojineer/debugger.hpp')
text = hpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    bool should_pause_before_execution() const override;',
    '    bool should_pause_before_execution() override;',
    'debugger pre-execution override')
hpp.write_text(text, encoding='utf-8')

cpp = Path('src/debugger.cpp')
text = cpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    'bool DebugController::should_pause_before_execution() const {\n'
    '    // If already paused, stay paused\n'
    '    if (paused_) {\n'
    '        return true;\n'
    '    }\n'
    '    \n'
    '    // If in paused mode, pause before executing\n'
    '    if (run_mode_ == DebugRunMode::Paused) {\n'
    '        return true;\n'
    '    }\n'
    '    \n'
    '    // Check for breakpoint hit - but don\'t re-hit the same breakpoint immediately\n'
    '    // after a step operation (handled by step semantics)\n'
    '    if (is_breakpoint_hit()) {\n'
    '        return true;\n'
    '    }\n'
    '    \n'
    '    // Check if we should pause for step (but only after the instruction has executed,\n'
    '    // so this is handled by debug_hook() which is called post-instruction)\n'
    '    // This method is for PRE-execution checks only\n'
    '    \n'
    '    return false;\n'
    '}',
    'bool DebugController::should_pause_before_execution() {\n'
    '    // Synchronize the newly initialized production VM before any instruction can run.\n'
    '    const Chunk* chunk = vm_.current_chunk();\n'
    '    if (chunk != current_chunk_) {\n'
    '        current_chunk_ = chunk;\n'
    '        rebuild_breakpoint_index();\n'
    '    }\n'
    '\n'
    '    // Initial debugger preparation is a zero-effect pause.\n'
    '    if (paused_ || run_mode_ == DebugRunMode::Paused) return true;\n'
    '\n'
    '    // A real source breakpoint pauses before the mapped instruction executes.\n'
    '    if (is_breakpoint_hit()) {\n'
    '        paused_ = true;\n'
    '        pause_reason_ = "breakpoint hit";\n'
    '        notify_debug_event(DebugEventType::BreakpointHit, pause_reason_);\n'
    '        return true;\n'
    '    }\n'
    '\n'
    '    return false;\n'
    '}',
    'pre-execution breakpoint binding and event')
cpp.write_text(text, encoding='utf-8')


# Replace stale test fixtures with canonical Emojineer identifiers and make the input
# non-consumption test execute a real 📥 after repeated debugger-only inspection.
tests = Path('tests/debugger_tests.cpp')
text = tests.read_text(encoding='utf-8')

start = text.index('void test_inspection_non_mutation() {')
end = text.index('// Test: Debugger inspection does not consume program input', start)
region = text[start:end]
region = region.replace('🐍 counter 🔢 🟰 0', '🐍 🍎 🔢 🟰 0')
region = region.replace('📝 counter', '📝 🍎')
region = region.replace('evaluate_expression("counter")', 'evaluate_expression("🍎")')
text = text[:start] + region + text[end:]

start = text.index('void test_inspection_does_not_consume_input() {')
end = text.index('// Test: Breakpoint binding diagnostics', start)
replacement = r'''void test_inspection_does_not_consume_input() {
    std::cout << "Test: debugger inspection does not consume program input...\n";

    const std::string source =
        "🐍 🍎 🔤 🟰 📥\n"
        "📝 🍎\n";

    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);

    std::istringstream input("kept-for-program\n");
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);

    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 1;
    bp.enabled = true;
    vm.add_breakpoint(bp);

    vm.execute(chunk); // initial zero-effect preparation pause
    require(input.tellg() == std::streampos(0), "preparation must not consume input");

    vm.continue_run();
    vm.execute(chunk); // bind and stop before the line-1 ReadLine instruction
    auto snapshot1 = vm.get_debug_snapshot();
    require(snapshot1.has_value(), "should pause before the real input read");
    require(snapshot1->current_position.line == 1, "input breakpoint should be on line 1");
    require(input.tellg() == std::streampos(0), "breakpoint must precede input consumption");

    auto snapshot2 = vm.get_debug_snapshot();
    auto snapshot3 = vm.get_debug_snapshot();
    (void)snapshot2;
    (void)snapshot3;
    (void)vm.get_breakable_positions();
    require(!vm.evaluate_expression("🍎").has_value(), "input-backed variable is not assigned before ReadLine");
    require(input.tellg() == std::streampos(0), "inspection must not consume program input");

    vm.continue_run();
    vm.execute(chunk);
    require(vm.is_finished(), "program should complete after resuming from input breakpoint");
    require(output.str() == "kept-for-program\n", "program must consume and print the original input exactly once");
    require(input.rdbuf()->in_avail() == 0, "the one supplied input line should be consumed exactly once");

    std::cout << "  ✅ Debugger inspection preserves program input\n";
}

'''
text = text[:start] + replacement + text[end:]

start = text.index('void test_source_map_function_context() {')
end = text.index('// Test: Bytecode roundtrip preserves source maps', start)
region = text[start:end].replace('➕️', '🚀')
text = text[:start] + region + text[end:]

start = text.index('void test_step_over_skips_inner_breakpoint() {')
end = text.index('} // anonymous namespace', start)
region = text[start:end]
region = region.replace('inner', '🚀').replace(' x ', ' 🍎 ').replace('(x)', '(🍎)').replace('📝 x', '📝 🍎').replace('📦 x', '📦 🍎')
text = text[:start] + region + text[end:]

tests.write_text(text, encoding='utf-8')
print('Train 18 debugger pre-execution, resume, and real input inspection repairs applied.')
