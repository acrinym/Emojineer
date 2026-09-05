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
path.write_text(text, encoding='utf-8')
print('Train 18 debugger numeric-value acceptance aligned with production Value semantics.')
