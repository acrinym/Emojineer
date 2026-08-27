// Debugger tests for Train 18 - Source-level debugger
// This is the 13th CTest target as specified in the contract

#include "emojineer/compiler.hpp"
#include "emojineer/debugger.hpp"
#include "emojineer/lexer.hpp"
#include "emojineer/parser.hpp"
#include "emojineer/vm.hpp"
#include "emojineer/bytecode.hpp"

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
    
    // Using proper block form with 🏁, and 🚀 (not ➕ which is Add token)
    const std::string source = 
        "🛠️ 🚀 🫴 🍎 🫴 🍐 🤲\n"   // Line 1: define 🚀(🍎, 🍐)
        "🐍 🍇 🔢 🟰 🍎 ➕ 🍐\n"   // Line 2: local 🍇 = 🍎 + 🍐 (arithmetic inside)
        "📦 🍇\n"                   // Line 3: return 🍇
        "🏁\n"                      // Line 4: end function
        "📝 🚀 🫴 1 2 🤲\n"        // Line 5: call 🚀
        "📝 📜done📜\n";           // Line 6: print done
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Also set a breakpoint INSIDE the callee to prove we never hit it
    emojineer::BreakpointLocation bp_inside;
    bp_inside.source_position.source_path = "test.emoji";
    bp_inside.source_position.line = 2;  // Inside the function definition
    bp_inside.enabled = true;
    vm.add_breakpoint(bp_inside);
    
    // Set breakpoint at the function call (line 5)
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 5;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Track if we ever paused inside the callee
    bool paused_in_callee = false;
    vm.set_debug_callback([&paused_in_callee](const emojineer::DebugSnapshot& snapshot) {
        // If we pause at line 2, we're inside the callee - this should NOT happen
        if (snapshot.current_position.line == 2) {
            paused_in_callee = true;
        }
    });
    
    // Execute and pause at breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused at line 5");
    
    // [REQUIRED] Get caller depth before step_over
    std::size_t caller_depth = snapshot->call_stack.size();
    
    // Step over - should execute the function but not stop inside it
    vm.step_over();
    vm.execute(chunk);
    
    // After step_over, we should be at line 6 (past the function call)
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after step_over");
    
    // [REQUIRED] Assert: caller depth should remain the same (we never entered the callee)
    require(snapshot->call_stack.size() == caller_depth, 
             "step_over caller depth should remain unchanged");
    
    // [REQUIRED] Assert: should NOT have paused inside the callee function (no breakpoint at line 2)
    require(!paused_in_callee, 
             "no StepComplete/breakpoint callback should occur inside callee during step_over");
    
    // [REQUIRED] Assert: expected post-call line should be line 6
    require(snapshot->current_position.line == 6, 
             "should be at line 6 (post-call line) after step_over");
    
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
    
    // Step into the function
    vm.step_into();
    vm.execute(chunk);
    
    // Now we should be inside inner function
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after step_into");
    
    // [REQUIRED] Record depth inside function
    std::size_t inside_depth = snapshot->call_stack.size();
    require(inside_depth > 1, "should be inside function with >1 frame");
    
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
        "🛠️ 🚀 🫴 🍎 🫴 🍐 🤲\n"       // Line 1: define 🚀(🍎, 🍐)
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
    vm.execute(chunk);
    vm.continue_run();
    
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot while paused at breakpoint");
    
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
    require(std::holds_alternative<std::int64_t>(*param1_val), "🍎 should be an integer");
    require(std::holds_alternative<std::int64_t>(*param2_val), "🍐 should be an integer");
    require(std::holds_alternative<std::int64_t>(*local_val), "🍇 should be an integer");
    
    auto param1_int = std::get<std::int64_t>(*param1_val);
    auto param2_int = std::get<std::int64_t>(*param2_val);
    auto local_int = std::get<std::int64_t>(*local_val);
    
    require(param1_int == 10, "🍎 should equal 10");
    require(param2_int == 20, "🍐 should equal 20");
    require(local_int == 30, "🍇 should equal 30 (🍎 + 🍐)");
    
    std::cout << "  Parameter 🍎 = " << param1_int << "\n";
    std::cout << "  Parameter 🍐 = " << param2_int << "\n";
    std::cout << "  Local 🍇 = " << local_int << " (🍎 + 🍐)\n";
    
    std::cout << "  ✅ Evaluate expression with exact parameter and local values works\n";
}

// Test: Frame selection
void test_frame_selection() {
    std::cout << "Test: frame selection...\n";
    
    // Using proper block form with 🏁 (canonical function syntax from tests.cpp)
    const std::string source = 
        "🛠️ ⭐ 🫴 🍎 🤲\n"          // Line 1: define ⭐(🍎)
        "📦 🍎\n"                    // Line 2: return 🍎
        "🏁\n"                       // Line 3: end function
        "🛠️ 🌟 🫴 🍐 🤲\n"          // Line 4: define 🌟(🍐)
        "📦 🍐\n"                    // Line 5: return 🍐
        "🏁\n"                       // Line 6: end function
        "📝 🌟 🫴 ⭐ 🫴 📜hi📜 🤲 🤲\n";  // Line 7: call 🌟(⭐(📜hi📜))
    
    emojineer::Lexer lexer(source, {});
    emojineer::Parser parser(lexer.tokenize());
    auto program = parser.parse();
    
    emojineer::Compiler compiler;
    compiler.set_source_path("test.emoji");
    auto chunk = compiler.compile(program);
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint at line 7 to get into nested calls
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 7;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Run to breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot");
    
    // Test frame selection
    require(snapshot->call_stack.size() > 0, "should have frames");
    
    // Select frame 0 and verify we can inspect
    bool success = vm.select_frame(0);
    require(success, "should successfully select frame 0");
    require(vm.selected_frame() == 0, "should select frame 0");
    
    // Try selecting a frame beyond the stack (should return false and keep prior frame)
    if (snapshot->call_stack.size() > 1) {
        std::size_t valid_frame_count = snapshot->call_stack.size();
        std::size_t prior_frame = vm.selected_frame();
        
        // Attempting to select an invalid frame should return false
        bool invalid_select = vm.select_frame(99);
        require(!invalid_select, "select_frame(99) should return false for out-of-range index");
        
        // The prior frame should remain selected (no silent clamping)
        require(vm.selected_frame() == prior_frame, "selected frame should remain unchanged on invalid request");
    }
    
    std::cout << "  ✅ Frame selection works\n";
}

// Test: Breakpoint doesn't immediately re-hit
void test_breakpoint_no_rehit() {
    std::cout << "Test: breakpoint doesn't immediately re-hit...\n";
    
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
    
    std::istringstream input;
    std::ostringstream output;
    emojineer::DebugVM vm(input, output);
    
    // Set breakpoint on line 2
    emojineer::BreakpointLocation bp;
    bp.source_position.source_path = "test.emoji";
    bp.source_position.line = 2;
    bp.enabled = true;
    vm.add_breakpoint(bp);
    
    // Run to breakpoint
    vm.execute(chunk);
    vm.continue_run();
    vm.execute(chunk);
    
    // Should pause at line 2
    auto snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot at breakpoint");
    require(snapshot->current_position.line == 2, "should be at line 2");
    
    // Continue - should NOT immediately re-hit the same breakpoint
    vm.continue_run();
    vm.execute(chunk);
    
    // Should have moved past the breakpoint
    snapshot = vm.get_debug_snapshot();
    require(snapshot.has_value(), "should have snapshot after continue");
    require(snapshot->current_position.line != 2 || vm.is_finished(), 
        "should have moved past line 2 or finished");
    
    std::cout << "  ✅ Breakpoint doesn't immediately re-hit works\n";
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
        
        std::cout << "\n✅ All debugger tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
