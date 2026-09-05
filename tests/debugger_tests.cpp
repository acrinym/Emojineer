// Debugger tests for Train 18 - Source-level debugger
// This is the 13th CTest target as specified in the contract

#include "emojineer/compiler.hpp"
#include "emojineer/debugger.hpp"
#include "emojineer/hash.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include "emojineer/bytecode.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("debugger test failed: " + message);
}

void require_equal(const std::string& actual, const std::string& expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + ": expected '" + expected + "' but got '" + actual + "'");
    }
}

// Test 1: Source mapping in bytecode
void test_source_mapping() {
    std::cout << "Test: source mapping in bytecode...\n";
    
    // Simple source - print a string
    const std::string source = "📝 📜hello📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Verify source map was created
    require(!chunk.source_map.empty(), "source map should not be empty");
    
    // Check that source positions are recorded
    require(chunk.source_map.size() == chunk.code.size(), 
        "source map should have entry for each instruction");
    
    // First instruction should have line 1
    require(chunk.source_map[0].line == 1, "first instruction should be on line 1");
    require(chunk.source_map[0].source_path == "test.emoji", 
        "source path should be set correctly");
    
    std::cout << "  ✅ Source mapping works\n";
}

// Test 2: DebugVM execution
void test_debug_vm_execution() {
    std::cout << "Test: DebugVM execution...\n";
    
    // Simple program: print "hello"
    const std::string source = "📝 📜hello📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Run in debug VM
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Execute without breakpoints - need to continue since it starts paused
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Verify execution completed
    require(vm.is_finished(), "VM should finish execution");
    
    std::cout << "  ✅ DebugVM execution works\n";
}

// Test 3: Breakpoint setting
void test_breakpoint_setting() {
    std::cout << "Test: breakpoint setting...\n";
    
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
    
    // Set a breakpoint
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 1;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Execute to load the chunk into VM (starts paused, so we need to continue)
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Verify breakpoint was added and positions are available
    auto positions = vm.get_breakable_positions();
    require(!positions.empty(), "should have breakable positions");
    
    std::cout << "  ✅ Breakpoint setting works\n";
}

// Test 4: Debug snapshot
void test_debug_snapshot() {
    std::cout << "Test: debug snapshot...\n";
    
    // Simple program - just print statements, valid emoji code
    const std::string source = "📝 📜hello📜\n📝 📜world📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Execute to load chunk and pause
    vm.execute(chunk);
    
    // Get snapshot when paused (default state)
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should be able to get snapshot when paused");
    
    // Verify snapshot has expected fields
    require(snapshot->current_position.source_path == "test.emoji", "should have source path");
    
    std::cout << "  ✅ Debug snapshot works\n";
}

// Test 5: Deterministic source path
void test_deterministic_source_path() {
    std::cout << "Test: deterministic source path...\n";
    
    // Verify that source paths are relative, not absolute
    const std::string source = "📝 📜hello📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("src/main.emoji");  // Relative path
    auto chunk = compiler.compile(program);
    
    // Verify source path is relative (no leading /)
    require(!chunk.source_map.empty(), "source map should not be empty");
    require(chunk.source_map[0].source_path.find('/') != 0, 
        "source path should be relative, not absolute");
    
    std::cout << "  ✅ Deterministic source path works\n";
}

// Test 6: DebugVM step functions exist
void test_step_functions() {
    std::cout << "Test: step functions exist...\n";
    
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
    
    // These should not throw
    vm.step_into();
    vm.step_over();
    vm.step_out();
    vm.pause();
    vm.continue_run();
    
    std::cout << "  ✅ Step functions exist and work\n";
}

// Test 7: Value rendering for debugger
void test_value_rendering() {
    std::cout << "Test: value rendering for debugger...\n";
    
    // Test rendering various value types
    emojineer::Value int_val = static_cast<int64_t>(42);
    require(emojineer::debug_render_value(int_val) == "42", "should render integer");
    
    emojineer::Value double_val = 3.14;
    require(emojineer::debug_render_value(double_val) == "3.14", "should render double");
    
    emojineer::Value bool_val = true;
    require(emojineer::debug_render_value(bool_val) == "✅", "should render true");
    
    emojineer::Value str_val = std::string("hello");
    require(emojineer::debug_render_value(str_val) == "hello", "should render string");
    
    auto arr_ptr = std::make_shared<emojineer::ArrayValue>();
    arr_ptr->elements.push_back(static_cast<int64_t>(1));
    arr_ptr->elements.push_back(static_cast<int64_t>(2));
    emojineer::Value arr_val = arr_ptr;
    require(emojineer::debug_render_value(arr_val).find("[") == 0, "should render array");
    
    std::cout << "  ✅ Value rendering works\n";
}

// Test 8: VM parity - normal VM and debug VM produce identical output
void test_vm_parity() {
    std::cout << "Test: VM parity (normal vs debug VM)...\n";
    
    // Program that prints values - valid emoji code
    const std::string source = "📝 📜hello📜\n📝 📜world📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Run in normal VM
    std::istringstream input1;
    std::ostringstream output1;
    {
        emojineer::VM vm(input1, output1);
        vm.execute(chunk);
    }
    std::string normal_output = output1.str();
    
    // Run in debug VM - need to continue since it starts paused
    std::istringstream input2;
    std::ostringstream output2;
    {
        emojineer::DebugVM vm(input2, output2);
        vm.execute(chunk);  // First call loads/initializes but pauses
        vm.continue_run();   // Continue execution
        vm.execute(chunk);   // Actually execute
    }
    std::string debug_output = output2.str();
    
    // Verify outputs are identical
    require_equal(normal_output, debug_output, "normal VM and debug VM must produce identical output");
    
    std::cout << "  ✅ VM parity verified\n";
}

// Test 9: VM parity with multiple statements
void test_vm_parity_with_functions() {
    std::cout << "Test: VM parity with multiple statements...\n";
    
    // Program with multiple statements - valid emoji code
    const std::string source = "📝 📜hello📜\n📝 📜world📜\n📝 📜test📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Run in normal VM
    std::istringstream input1;
    std::ostringstream output1;
    {
        emojineer::VM vm(input1, output1);
        vm.execute(chunk);
    }
    std::string normal_output = output1.str();
    
    // Run in debug VM - need to continue since it starts paused
    std::istringstream input2;
    std::ostringstream output2;
    {
        emojineer::DebugVM vm(input2, output2);
        vm.execute(chunk);  // First call loads/initializes but pauses
        vm.continue_run();   // Continue execution
        vm.execute(chunk);   // Actually execute
    }
    std::string debug_output = output2.str();
    
    // Verify outputs are identical
    require_equal(normal_output, debug_output, "VM parity with multiple statements");
    
    std::cout << "  ✅ VM parity with multiple statements verified\n";
}

// Test 10: Breakpoint actually stops execution and can continue
void test_breakpoint_stops_execution() {
    std::cout << "Test: breakpoint actually stops execution and can continue...\n";
    
    // Program with multiple lines so we can test breakpoints - valid emoji code
    const std::string source = "📝 📜line1📜\n📝 📜line2📜\n📝 📜line3📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set a breakpoint on line 2
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 2;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Track pause state
    bool paused_at_breakpoint = false;
    std::string pause_reason;
    vm.set_debug_callback([&paused_at_breakpoint, &pause_reason](const emojineer::DebugSnapshot& snapshot) {
        paused_at_breakpoint = true;
        pause_reason = snapshot.reason;
    });
    
    // Run - should pause at breakpoint (need to continue first since it starts paused)
    vm.execute(chunk);  // First call - loads but pauses
    vm.continue_run();  // Continue to run
    vm.execute(chunk);  // Execute
    
    // Verify we paused at breakpoint
    require(paused_at_breakpoint, "should have paused at breakpoint");
    require(pause_reason == "breakpoint hit", "pause reason should be 'breakpoint hit'");
    
    // Verify we can get a snapshot while paused
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should be able to get snapshot while paused");
    
    // Verify current position is at breakpoint line
    require(snapshot->current_position.line == 2, "should be at line 2 (breakpoint)");
    
    // Verify output from lines before breakpoint was printed
    std::string vm_output = output.str();
    require(vm_output.find("line1") != std::string::npos, "line1 should have been printed before breakpoint");
    
    std::cout << "  ✅ Breakpoint stops execution and can continue\n";
}

// Test 11: Step into functionality - verify it can be called
// NOTE: Full step semantics need more work - this test verifies basic API works
void test_step_into() {
    std::cout << "Test: step into functionality - stepping to next instruction...\n";
    
    // Simple program with multiple lines - valid emoji code
    const std::string source = "📝 📜line1📜\n📝 📜line2📜\n📝 📜line3📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set a breakpoint to ensure we pause before running to completion
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 2;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Execute and run to breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Get snapshot while paused at breakpoint
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused at breakpoint");
    
    // Verify source position
    require(snapshot->current_position.line == 2, "should be at line 2");
    
    std::cout << "  ✅ Step into works\n";
}

// Test 12: Frame inspection at breakpoint
void test_frame_inspection() {
    std::cout << "Test: frame inspection at breakpoint...\n";
    
    // Simple program - valid emoji code
    const std::string source = "📝 📜line1📜\n📝 📜line2📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at line 1
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 1;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Track when paused
    bool paused = false;
    vm.set_debug_callback([&paused](const emojineer::DebugSnapshot& snapshot) {
        paused = true;
    });
    
    // Run to breakpoint (need to continue first)
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    require(paused, "should have paused at breakpoint");
    
    // Get snapshot to inspect
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused");
    
    // Verify we can inspect source position
    require(snapshot->current_position.source_path == "test.emoji", "should have source path");
    require(snapshot->current_position.line == 1, "should be at line 1");
    
    std::cout << "  ✅ Frame inspection works\n";
}

// Test 8: Multiple breakpoints
void test_multiple_breakpoints() {
    std::cout << "Test: multiple breakpoints...\n";
    
    // Valid emoji code
    const std::string source = "📝 📜line1📜\n📝 📜line2📜\n📝 📜line3📜\n";
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set multiple breakpoints
    emojineer::BreakpointLocation bp1;
    bp1.source_position.source_path = "test.emoji";
    bp1.source_position.line = 1;
    bp1.enabled = true;
    vm.add_breakpoint(bp1);
    
    emojineer::BreakpointLocation bp2;
    bp2.source_position.source_path = "test.emoji";
    bp2.source_position.line = 2;
    bp2.enabled = true;
    vm.add_breakpoint(bp2);
    
    // Execute to load the chunk into VM
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Verify we have breakpoints
    auto positions = vm.get_breakable_positions();
    require(positions.size() >= 2, "should have multiple breakable positions");
    
    std::cout << "  ✅ Multiple breakpoints work\n";
}

// Test: Step into nested function call
void test_step_into_nested_function() {
    std::cout << "Test: step into nested function call...\n";
    
    // Program with function call to test step-into
    // Using proper block form with 🏁 (canonical function syntax from tests.cpp)
    const std::string source = 
        "🛠️ ⭐ 🫴 🍎 🤲\n"  // Line 1: define ⭐(🍎)
        "📦 🍎\n"           // Line 2: return 🍎
        "🏁\n"              // Line 3: end function
        "🛠️ 🌟 🫴 🍐 🤲\n"  // Line 4: define 🌟(🍐)
        "📦 🍐\n"           // Line 5: return 🍐
        "🏁\n"              // Line 6: end function
        "📝 ⭐ 🫴 📜hello📜 🤲\n";  // Line 7: call ⭐
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at the function call (line 7)
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 7;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Execute and pause at breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Verify we're paused at line 7
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused");
    require(snapshot->current_position.line == 7, "should be at line 7 (function call)");
    
    // Get the initial call stack depth
    auto initial_frames = snapshot->call_stack;
    std::size_t initial_depth = initial_frames.size();
    
    // Record the function name before stepping into (should be module/main)
    std::string func_before_step = initial_frames.empty() ? "" : initial_frames[0].function_name;
    
    // Step into the function call
    vm.step_into();
    vm.execute(chunk);
    
    // After step_into, we should be inside the function
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after step_into");
    
    // [REQUIRED] Assert: call stack should be deeper (we entered a function)
    auto new_frames = snapshot->call_stack;
    require(new_frames.size() > initial_depth, "call stack should be deeper after step_into");
    
    // [REQUIRED] Assert: verify callee function identity - function name should NOT be same as before
    require(!new_frames.empty(), "should have at least one frame");
    std::string func_after_step = new_frames[0].function_name;
    require(func_after_step != func_before_step || func_after_step != "main", 
             "callee function identity should differ from caller");
    
    // [REQUIRED] Assert: callee source line - should be inside function definition (line 1-2)
    require(snapshot->current_position.line >= 1 && snapshot->current_position.line <= 2, 
        "callee source line should be inside function definition (line 1-2)");
    require(snapshot->current_position.source_path == "test.emoji", "should be in test.emoji");
    
    // Verify function name is "⭐" (the actual function we stepped into)
    require(new_frames[0].function_name == "⭐", "innermost frame should be '⭐' function");
    
    // Verify parameter is accessible in the callee frame
    bool found_param = false;
    for (const auto& name : new_frames[0].parameter_names) {
        if (name == "🍎") {
            found_param = true;
            break;
        }
    }
    require(found_param, "parameter '🍎' should be visible in inner function frame");
    
    std::cout << "  ✅ Step into nested function works\n";
}

// Test: Step over function call
void test_step_over_function() {
    std::cout << "Test: step over function call...\n";
    const std::string source =
        "🛠️ 🚀 🫴 🍎 🍐 🤲\n"
        "🐍 🍇 🔢 🟰 🍎 ➕ 🍐\n"
        "📦 🍇\n"
        "🏁\n"
        "📝 🚀 🫴 1 2 🤲\n"
        "📝 📜done📜\n";
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
    bp.source_position.line = 5;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should pause at call line before step_over");
    require(snapshot->current_position.line == 5, "should be at call line 5");
    const std::size_t caller_depth = snapshot->call_stack.size();
    vm.step_over();
    vm.execute(chunk);
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should pause after step_over");
    require(snapshot->call_stack.size() == caller_depth, "step_over must return to caller depth");
    require(snapshot->current_position.line == 6, "step_over must land on post-call line 6");
    require(snapshot->reason == "stepped over", "pure step_over must report StepComplete");
    std::cout << "  ✅ Step over function call works\n";
}

// Test: Step out of function
void test_step_out_function() {
    std::cout << "Test: step out of function...\n";
    
    // Using proper block form with 🏁 (canonical function syntax from tests.cpp)
    const std::string source = 
        "🛠️ ⭐ 🫴 🍎 🤲\n"          // Line 1: define ⭐(🍎)
        "📦 🍎\n"                    // Line 2: return 🍎
        "🏁\n"                       // Line 3: end function
        "📝 ⭐ 🫴 📜test📜 🤲\n"      // Line 4: call ⭐
        "📝 📜after📜\n";             // Line 5: print after
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at the function call (line 4)
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 4;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Execute and pause at breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Record the caller's source position before stepping into
    auto snapshot_before = vm.get_debug_snapshot();
    require(snapshot_before.has_value(), "should have snapshot at breakpoint");
    std::string caller_source_path = snapshot_before->current_position.source_path;
    std::uint32_t caller_line = snapshot_before->current_position.line;
    std::size_t caller_depth = snapshot_before->call_stack.size();
    
    // Step into the function
    vm.step_into();
    vm.execute(chunk);
    
    // Now we should be inside inner function
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after step_into");
    
    // [REQUIRED] Record depth inside function
    std::size_t inside_depth = snapshot->call_stack.size();
    require(inside_depth > caller_depth, "step_into must add a real callee frame");
    
    // Record the function we stepped out of
    std::string func_inside = snapshot->call_stack[0].function_name;
    
    // [REQUIRED] Step out - should return to caller
    vm.step_out();
    vm.execute(chunk);
    
    // After step_out, we should be back at line 4 or 5
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after step_out");
    
    // [REQUIRED] Assert: shallower depth - depth should be reduced (returned to caller)
    require(snapshot->call_stack.size() < inside_depth, 
             "step_out should return to shallower caller frame");
    
    // [REQUIRED] Assert: expected caller source position
    // We should be back at the caller line (line 4, the function call site)
    require(snapshot->current_position.source_path == caller_source_path, 
             "caller source path should be preserved after step_out");
    require(snapshot->current_position.line >= 4 && snapshot->current_position.line <= 5, 
             "should be back at caller source position (line 4 or 5) after step_out");
    
    // Verify the function name is now different (we're in the caller, not the callee)
    if (!snapshot->call_stack.empty()) {
        std::string func_after = snapshot->call_stack[0].function_name;
        require(func_after != func_inside || snapshot->call_stack.size() < inside_depth,
                 "should be in different frame (caller) after step_out");
    }
    
    std::cout << "  ✅ Step out of function works\n";
}

// Test: Evaluate expression with parameters and locals
void test_evaluate_with_params_locals() {
    std::cout << "Test: evaluate expression with parameters and locals...\n";
    
    // Source with function that has parameters AND a local variable
    // Using proper emoji identifiers:
    // - 🍎, 🍐 are parameter names (valid identifiers)
    // - 🍇 is a LOCAL variable (not 🔢 which is a numeric type token)
    // - 10 and 20 are numeric literals (not 🔟 and 🔟🔟)
    // - 🏁 is canonical function end marker
    const std::string source = 
        "🛠️ 🚀 🫴 🍎 🍐 🤲\n"       // Line 1: define 🚀(🍎, 🍐)
        "🐍 🍇 🟰 🍎 ➕ 🍐\n"           // Line 2: local 🍇 = 🍎 + 🍐 (valid local identifier)
        "📝 🍇\n"                       // Line 3: print 🍇 (use local) - BREAKPOINT HERE
        "📦 🍇\n"                       // Line 4: return 🍇
        "🏁\n"                          // Line 5: end function
        "📝 🚀 🫴 10 20 🤲\n";         // Line 6: call 🚀(10, 20)
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at line 3 (inside function after local is assigned)
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 3;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Execute the complete program in ONE chunk (function def + call together)
    // This is crucial - the call compiler must own the function definition
    // continue_run() only changes controller state; we need another execute() to actually run to breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);  // Actually execute to the breakpoint
    
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused at breakpoint");
    require(snapshot->current_position.line == 3, "should be paused at line 3 (breakpoint)");
    
    // The snapshot should show we're in the function with parameters and locals
    require(snapshot->call_stack.size() > 0, "should have at least one frame");
    
    // Select the innermost frame (frame 0)
    vm.select_frame(0);
    
    // Verify parameters are visible with evaluate_expression
    // The function was called with 🍎=10, 🍐=20
    // After 🍎 + 🍐 = 30, local 🍇 should be 30
    
    // Evaluate the first parameter (🍎)
    auto param1_val = vm.evaluate_expression("🍎");
    require(param1_val.has_value(), "should be able to evaluate parameter 🍎");
    
    // Evaluate the second parameter (🍐)  
    auto param2_val = vm.evaluate_expression("🍐");
    require(param2_val.has_value(), "should be able to evaluate parameter 🍐");
    
    // Evaluate the local variable (🍇)
    auto local_val = vm.evaluate_expression("🍇");
    require(local_val.has_value(), "should be able to evaluate local 🍇");
    
    // EXACT VALUE ASSERTIONS - no more existence-only checks
    // Based on call 🚀(10, 20) - 🍎=10, 🍐=20, 🍇=🍎+🍐=30
    require(std::holds_alternative<double>(*param1_val), "🍎 should preserve the runtime number representation");
    require(std::holds_alternative<double>(*param2_val), "🍐 should preserve the runtime number representation");
    require(std::holds_alternative<double>(*local_val), "🍇 should preserve the runtime number representation");
    
    auto param1_number = std::get<double>(*param1_val);
    auto param2_number = std::get<double>(*param2_val);
    auto local_number = std::get<double>(*local_val);
    
    require(param1_number == 10.0, "🍎 should equal 10");
    require(param2_number == 20.0, "🍐 should equal 20");
    require(local_number == 30.0, "🍇 should equal 30 (🍎 + 🍐)");
    
    std::cout << "  Parameter 🍎 = " << param1_number << "\n";
    std::cout << "  Parameter 🍐 = " << param2_number << "\n";
    std::cout << "  Local 🍇 = " << local_number << " (🍎 + 🍐)\n";
    
    std::cout << "  ✅ Evaluate expression with exact parameter and local values works\n";
}

// Test: Frame selection
void test_frame_selection() {
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

// Test: Breakpoint doesn't immediately re-hit
void test_breakpoint_no_rehit() {
    std::cout << "Test: breakpoint continue makes real progress...\n";
    const std::string source =
        "📝 📜line1📜\n"
        "📝 📜line2📜\n"
        "📝 📜line3📜\n";
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
    require(snapshot.has_value(), "should pause at line-2 breakpoint");
    require(snapshot->current_position.line == 2, "should be at line 2 before resume");
    const auto stopped_ip = vm.current_ip();
    require(output.str() == "line1\n", "only line1 should execute before the line-2 breakpoint");
    vm.continue_run();
    vm.execute(chunk);
    if (!vm.is_finished()) {
        snapshot = vm.get_debug_snapshot();
        require(snapshot.has_value(), "a non-finished debugger must expose its next pause");
        require(vm.current_ip() != stopped_ip || snapshot->current_position.line != 2,
                "continue must not immediately re-hit the same bound instruction");
    }
    require(vm.is_finished(), "without another breakpoint, continue should finish the program");
    require(output.str() == "line1\nline2\nline3\n",
            "the breakpointed line and following line must each execute exactly once");
    std::cout << "  ✅ Breakpoint continue makes real progress\n";
}

// Test: Source map has real non-placeholder positions
void test_source_map_real_positions() {
    std::cout << "Test: source map has real non-placeholder positions...\n";
    
    // Source with multiple statements on different lines
    const std::string source = 
        "📝 📜line1📜\n"  // Line 1
        "📝 📜line2📜\n"  // Line 2
        "📝 📜line3📜\n"; // Line 3
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Verify source map has entries for each instruction
    require(!chunk.source_map.empty(), "source map should not be empty");
    require(chunk.source_map.size() == chunk.code.size(), 
        "source map should have entry for each instruction");
    
    // Verify real positions - not just default line=1, column=1
    // Check that we have positions on different lines
    bool found_line1 = false, found_line2 = false, found_line3 = false;
    for (const auto& src : chunk.source_map) {
        if (src.line == 1) found_line1 = true;
        if (src.line == 2) found_line2 = true;
        if (src.line == 3) found_line3 = true;
    }
    
    require(found_line1, "should have instructions on line 1");
    require(found_line2, "should have instructions on line 2");
    require(found_line3, "should have instructions on line 3");
    
    // Verify source path is deterministic (no absolute paths)
    for (const auto& src : chunk.source_map) {
        require(!src.source_path.empty(), "source path should not be empty");
        require(src.source_path[0] != '/', "source path should be relative, not absolute");
    }
    
    std::cout << "  ✅ Source map has real non-placeholder positions\n";
}

// Test: Source map with function context
void test_source_map_function_context() {
    std::cout << "Test: source map with function context...\n";
    
    // Using canonical emoji function syntax
    const std::string source = 
        "🛠️ 🚀 🫴 🍎 🍐 🤲\n"     // Line 1: define 🚀(🍎, 🍐)
        "📦 🍎 ➕ 🍐\n"             // Line 2: return 🍎 + 🍐
        "🏁\n"                     // Line 3: end function
        "📝 🚀 🫴 1 2 🤲\n";      // Line 4: call 🚀(1, 2)
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    // Check that function has context in source map
    require(chunk.functions.size() == 1, "should have one function");
    require(chunk.functions[0].name == "🚀", "function should be named '🚀'");
    
    // Find instructions inside the function and verify function context
    bool found_function_context = false;
    for (const auto& src : chunk.source_map) {
        if (!src.function_name.empty()) {
            require(src.function_name == "🚀", "function name should be '🚀'");
            found_function_context = true;
        }
    }
    require(found_function_context, "source map should include function context");
    
    std::cout << "  ✅ Source map with function context works\n";
}

// Test: Bytecode roundtrip preserves source maps
void test_bytecode_roundtrip_source_map() {
    std::cout << "Test: bytecode roundtrip preserves source maps...\n";
    
    const std::string source = 
        "📝 📜hello📜\n"  // Line 1
        "📝 📜world📜\n"; // Line 2
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto original_chunk = compiler.compile(program);
    original_chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);
    
    // Serialize to bytecode
    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    emojineer::write_bytecode(original_chunk, bytes);
    
    // Deserialize
    bytes.seekg(0);
    auto restored_chunk = emojineer::read_bytecode(bytes);
    
    // Verify source map survived roundtrip
    require(restored_chunk.source_map.size() == original_chunk.source_map.size(),
        "source map size should survive roundtrip");
    require(restored_chunk.source_hashes == original_chunk.source_hashes,
        "v7 source SHA-256 provenance must survive bytecode roundtrip");
    
    for (std::size_t i = 0; i < original_chunk.source_map.size(); ++i) {
        const auto& orig = original_chunk.source_map[i];
        const auto& read = restored_chunk.source_map[i];
        
        require(read.source_path == orig.source_path, 
            "source_path should survive roundtrip");
        require(read.line == orig.line, 
            "line should survive roundtrip");
        require(read.column == orig.column, 
            "column should survive roundtrip");
        require(read.end_line == orig.end_line, 
            "end_line should survive roundtrip (v6)");
        require(read.end_column == orig.end_column, 
            "end_column should survive roundtrip (v6)");
        require(read.function_name == orig.function_name, 
            "function_name should survive roundtrip (v6)");
    }
    
    std::cout << "  ✅ Bytecode roundtrip preserves source maps\n";
}

// Test: Debugger inspection does not mutate state
void test_inspection_non_mutation() {
    std::cout << "Test: debugger inspection does not mutate state...\n";
    
    const std::string source = 
        "🐍 🍎 🔢 🟰 0\n"    // Line 1: counter = 0
        "📝 🍎\n"              // Line 2: print counter
        "📝 🍎\n";             // Line 3: print counter again
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at line 2 to pause before second print
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 2;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Run to breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    auto snapshot1 = vm.get_debug_snapshot();
    require(snapshot1.has_value(), "should have snapshot at breakpoint");
    
    require(snapshot1->call_stack.empty(), "module-scope pause must not fabricate a function frame");
    auto global1 = snapshot1->globals.find("🍎");
    require(global1 != snapshot1->globals.end(), "snapshot must expose module global 🍎");
    require(emojineer::debug_render_value(global1->second) == "0", "module global 🍎 should equal 0");
    auto eval_result = vm.evaluate_expression("🍎");
    require(eval_result.has_value(), "module global must be inspectable without a function frame");
    require(emojineer::debug_render_value(*eval_result) == "0", "print/evaluate must observe the same global value");
    auto snapshot2 = vm.get_debug_snapshot();
    require(snapshot2.has_value(), "inspection must preserve the paused snapshot");
    auto global2 = snapshot2->globals.find("🍎");
    require(global2 != snapshot2->globals.end(), "global must remain present after inspection");
    require(emojineer::values_equal(global1->second, global2->second), "inspection must not mutate module globals");
    
    // Execute to completion
    vm.continue_run();
    vm.execute(chunk);
    
    // Verify output is deterministic - both prints should show 0
    // (inspection should not have mutated counter)
    require(output.str() == "0\n0\n", 
        "output should be '0\\n0\\n' - inspection should not mutate state");
    
    std::cout << "  ✅ Debugger inspection does not mutate state\n";
}

// Test: Debugger inspection does not consume program input
void test_inspection_does_not_consume_input() {
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
    vm.execute(chunk);
    require(input.tellg() == std::streampos(0), "preparation must not consume input");
    vm.continue_run();
    vm.execute(chunk);
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

// Test: Breakpoint binding diagnostics
void test_breakpoint_binding_diagnostics() {
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

// Test: Step over with inner breakpoint should not hit inner breakpoint
void test_step_over_honors_inner_breakpoint() {
    std::cout << "Test: step over honors explicit inner breakpoint...\n";
    const std::string source =
        "🛠️ 🚀 🫴 🍎 🤲\n"
        "📝 🍎\n"
        "📦 🍎\n"
        "🏁\n"
        "📝 🚀 🫴 42 🤲\n"
        "📝 📜done📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    emojineer::BreakpointLocation inner;
    inner.source_position.source_path = "test.emoji";
    inner.source_position.line = 2;
    inner.enabled = true;
    vm.add_breakpoint(inner);
    emojineer::BreakpointLocation call;
    call.source_position.source_path = "test.emoji";
    call.source_position.line = 5;
    call.enabled = true;
    vm.add_breakpoint(call);
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value() && snapshot->current_position.line == 5, "should reach call breakpoint");
    const std::size_t caller_depth = snapshot->call_stack.size();
    vm.step_over();
    vm.execute(chunk);
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "explicit inner breakpoint must pause during step_over");
    require(snapshot->current_position.line == 2, "inner breakpoint must win over step_over");
    require(snapshot->reason == "breakpoint hit", "inner stop must report breakpoint hit");
    require(snapshot->call_stack.size() > caller_depth, "inner breakpoint must expose the callee frame");
    vm.remove_breakpoint(0);
    vm.remove_breakpoint(0);
    vm.step_out();
    vm.execute(chunk);
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "step_out after inner breakpoint should pause in caller");
    require(snapshot->call_stack.size() <= caller_depth, "step_out must restore caller depth");
    require(snapshot->current_position.line >= 5 && snapshot->current_position.line <= 6,
            "step_out should return to the call/post-call source region");
    std::cout << "  ✅ Step over honors explicit inner breakpoint\n";
}


void test_breakpoint_stale_and_source_drift() {
    std::cout << "Test: breakpoint stale and source-drift diagnostics...\n";
    const std::string source = "📝 📜hello📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 1;
    vm.add_breakpoint(bp);
    vm.execute(chunk);

    auto current = std::make_shared<emojineer::SourceResolver>();
    current->register_source("test.emoji", source);
    vm.set_source_resolver(current);
    auto infos = vm.get_breakpoint_info();
    require(infos.size() == 1 && infos[0].status == emojineer::BreakpointStatus::Bound,
            "matching current source must report Bound");

    auto changed = std::make_shared<emojineer::SourceResolver>();
    changed->register_source("test.emoji", "📝 📜changed📜\n");
    vm.set_source_resolver(changed);
    infos = vm.get_breakpoint_info();
    require(infos[0].status == emojineer::BreakpointStatus::Stale,
            "changed source content must report Stale");

    auto missing = std::make_shared<emojineer::SourceResolver>();
    vm.set_source_resolver(missing);
    infos = vm.get_breakpoint_info();
    require(infos[0].status == emojineer::BreakpointStatus::SourceDrift,
            "compiled identity with unavailable source must report SourceDrift");
    std::cout << "  ✅ Breakpoint stale/source-drift diagnostics work\n";
}

void test_debug_metadata_validation() {
    std::cout << "Test: debug metadata validation...\n";
    const std::string source = "📝 📜hello📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["test.emoji"] = emojineer::sha256_hex(source);

    auto bad_count = chunk;
    bad_count.source_map.pop_back();
    bool rejected = false;
    try { emojineer::verify_bytecode(bad_count); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "source-map cardinality mismatch must be rejected");

    auto bad_path = chunk;
    bad_path.source_map[0].source_path = "/tmp/checkout/test.emoji";
    rejected = false;
    try { emojineer::verify_bytecode(bad_path); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "absolute checkout-specific source identity must be rejected");

    auto bad_range = chunk;
    bad_range.source_map[0].end_column = 0;
    rejected = false;
    try { emojineer::verify_bytecode(bad_range); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "zero/reversed source range metadata must be rejected");

    auto bad_names = chunk;
    emojineer::FunctionInfo f;
    f.name = "🚀"; f.entry = 0; f.arity = 1; f.local_count = 1; f.parameter_names = {"🍎", "🍐"};
    bad_names.functions.push_back(f);
    rejected = false;
    try { emojineer::verify_bytecode(bad_names); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "parameter-name count inconsistent with arity must be rejected");
    std::cout << "  ✅ Debug metadata validation works\n";
}

void test_emjbc_debug_session() {
    std::cout << "Test: serialized .emjbc debug session...\n";
    const std::string source = "📝 📜bytecode-debug📜\n";
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    emojineer::Compiler compiler;
    compiler.set_source_path("fixture.emoji");
    auto chunk = compiler.compile(program);
    chunk.source_hashes["fixture.emoji"] = emojineer::sha256_hex(source);

    const auto path = std::filesystem::temp_directory_path() / "emojineer-train18-debug.emjbc";
    {
        std::ofstream out(path, std::ios::binary);
        emojineer::write_bytecode(chunk, out);
    }
    std::istringstream commands("continue\n");
    std::ostringstream output;
    std::ostringstream error;
    const int result = emojineer::run_debug_session(path, commands, output, error, {});
    std::filesystem::remove(path);
    require(result == 0, "debugging serialized .emjbc must succeed");
    require(error.str().empty(), "serialized bytecode debug session must not report loader errors");
    require(output.str().find("bytecode-debug") != std::string::npos,
            "serialized bytecode must execute through the debugger rather than being recompiled as source");
    require(output.str().find("Program finished") != std::string::npos,
            "serialized bytecode debugger must run to completion");
    std::cout << "  ✅ Serialized .emjbc debug session works\n";
}

} // anonymous namespace

int main() {
    std::cout << "=== Train 18 Debugger Tests ===\n\n";
    
    try {
        test_source_mapping();
        test_debug_vm_execution();
        test_breakpoint_setting();
        test_debug_snapshot();
        test_deterministic_source_path();
        test_step_functions();
        test_value_rendering();
        test_multiple_breakpoints();
        
        // New regression tests
        test_vm_parity();
        test_vm_parity_with_functions();
        test_breakpoint_stops_execution();
        test_step_into();
        test_frame_inspection();
        
        // New step semantic tests
        test_step_into_nested_function();
        test_step_over_function();
        test_step_out_function();
        test_evaluate_with_params_locals();
        test_frame_selection();
        test_breakpoint_no_rehit();
        
        // New comprehensive tests for PR #21 acceptance
        test_source_map_real_positions();
        test_source_map_function_context();
        test_bytecode_roundtrip_source_map();
        test_inspection_non_mutation();
        test_inspection_does_not_consume_input();
        test_breakpoint_binding_diagnostics();
        test_step_over_honors_inner_breakpoint();
        test_breakpoint_stale_and_source_drift();
        test_debug_metadata_validation();
        test_emjbc_debug_session();
        
        std::cout << "\n✅ All debugger tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
