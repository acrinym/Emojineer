from pathlib import Path
p = Path('tests/debugger_tests.cpp')
text = p.read_text(encoding='utf-8')
anchor = '#include <iostream>\n'
replacement = '#include <filesystem>\n#include <fstream>\n#include <iostream>\n'
if replacement not in text:
    if anchor not in text:
        raise SystemExit('debugger test include anchor missing')
    text = text.replace(anchor, replacement, 1)
p.write_text(text, encoding='utf-8')
print('Train 18 serialized-bytecode test includes added.')
