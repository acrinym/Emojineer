from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)

# Snapshots expose shared VM globals independently of function frames.
types = Path('include/emojineer/debug_types.hpp')
text = types.read_text(encoding='utf-8')
text = replace_once(
    text,
    'struct DebugSnapshot {\n    std::vector<DebugFrame> call_stack;\n    std::size_t current_frame;',
    'struct DebugSnapshot {\n    std::vector<DebugFrame> call_stack;\n    std::unordered_map<std::string, Value> globals;\n    std::size_t current_frame;',
    'snapshot globals')
types.write_text(text, encoding='utf-8')

cpp = Path('src/debugger.cpp')
text = cpp.read_text(encoding='utf-8')
text = replace_once(
    text,
    '    DebugSnapshot snapshot;\n    snapshot.call_stack = vm_.get_call_stack();',
    '    DebugSnapshot snapshot;\n    snapshot.call_stack = vm_.get_call_stack();\n    snapshot.globals = vm_.get_globals();',
    'snapshot globals population')

# Global inspection is valid at module scope even when there are no function frames.
old = '''    // Get the current frame's locals, parameters, and globals
    auto frames = vm_.get_call_stack();
    if (frames.empty()) {
        return std::nullopt;
    }
    
    // Clamp selected frame to valid range
    std::size_t frame_idx = std::min(selected_frame_, frames.size() - 1);
    
    // Trim whitespace from expression
'''
new = '''    // Function frames are optional at module scope; globals remain inspectable either way.
    auto frames = vm_.get_call_stack();
    std::size_t frame_idx = frames.empty() ? 0 : std::min(selected_frame_, frames.size() - 1);
    
    // Trim whitespace from expression
'''
text = replace_once(text, old, new, 'module-scope evaluate')

# CLI frame selection must not underflow/index an empty call stack.
needle = '''            // Accept optional frame index - use controller's frame selection
            if (parts.size() >= 2) {'''
replacement = '''            if (snapshot->call_stack.empty()) {
                output << "No function frames (paused at module scope)\\n";
                continue;
            }
            // Accept optional frame index - use controller's frame selection
            if (parts.size() >= 2) {'''
text = replace_once(text, needle, replacement, 'frame empty-stack guard')

needle = '''            // Use controller's selected frame
            std::size_t current_frame = debug_vm->selected_frame();
            if (current_frame >= snapshot->call_stack.size()) {
                current_frame = 0;
                debug_vm->select_frame(0);
            }
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Locals in frame #" << current_frame << ":\\n";'''
replacement = '''            if (snapshot->call_stack.empty()) {
                output << "No function frames (paused at module scope)\\n";
                continue;
            }
            // Use controller's selected frame
            std::size_t current_frame = debug_vm->selected_frame();
            if (current_frame >= snapshot->call_stack.size()) {
                current_frame = 0;
                debug_vm->select_frame(0);
            }
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Locals in frame #" << current_frame << ":\\n";'''
text = replace_once(text, needle, replacement, 'locals empty-stack guard')

old = '''            // Use controller's selected frame
            std::size_t current_frame = debug_vm->selected_frame();
            if (current_frame >= snapshot->call_stack.size()) {
                current_frame = 0;
            }
            const auto& frame = snapshot->call_stack[current_frame];
            output << "Globals in frame #" << current_frame << ":\\n";
            for (const auto& g : frame.globals) {
                output << "  " << g.first << " = " << emojineer::debug_render_value(g.second) << "\\n";
            }
            if (frame.globals.empty()) {
                output << "  (no globals)\\n";
            }'''
new = '''            output << "Globals:\\n";
            for (const auto& g : snapshot->globals) {
                output << "  " << g.first << " = " << emojineer::debug_render_value(g.second) << "\\n";
            }
            if (snapshot->globals.empty()) {
                output << "  (no globals)\\n";
            }'''
text = replace_once(text, old, new, 'module-scope globals CLI')
cpp.write_text(text, encoding='utf-8')

# Turn the crashing module-scope test into an exact read-only global-state assertion.
tests = Path('tests/debugger_tests.cpp')
text = tests.read_text(encoding='utf-8')
start = text.index('void test_inspection_non_mutation() {')
end = text.index('// Test: Debugger inspection does not consume program input', start)
region = text[start:end]
region = region.replace(
    '    // Get globals - this should be read-only\n    auto globals1 = snapshot1->call_stack[0].globals;\n    \n    // Evaluate an expression - this should be read-only\n    auto eval_result = vm.evaluate_expression("🍎");\n    \n    // Get snapshot again after inspection\n    auto snapshot2 = vm.get_debug_snapshot();',
    '    require(snapshot1->call_stack.empty(), "module-scope pause must not fabricate a function frame");\n'
    '    auto global1 = snapshot1->globals.find("🍎");\n'
    '    require(global1 != snapshot1->globals.end(), "snapshot must expose module global 🍎");\n'
    '    require(emojineer::debug_render_value(global1->second) == "0", "module global 🍎 should equal 0");\n'
    '    auto eval_result = vm.evaluate_expression("🍎");\n'
    '    require(eval_result.has_value(), "module global must be inspectable without a function frame");\n'
    '    require(emojineer::debug_render_value(*eval_result) == "0", "print/evaluate must observe the same global value");\n'
    '    auto snapshot2 = vm.get_debug_snapshot();\n'
    '    require(snapshot2.has_value(), "inspection must preserve the paused snapshot");\n'
    '    auto global2 = snapshot2->globals.find("🍎");\n'
    '    require(global2 != snapshot2->globals.end(), "global must remain present after inspection");\n'
    '    require(emojineer::values_equal(global1->second, global2->second), "inspection must not mutate module globals");')
text = text[:start] + region + text[end:]
tests.write_text(text, encoding='utf-8')
print('Train 18 module-scope snapshots, globals, CLI inspection, and regression repaired.')
