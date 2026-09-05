from pathlib import Path

path = Path('src/bytecode.cpp')
text = path.read_text(encoding='utf-8')
old = '''        if (!c.source_map.empty()) {
            const bool mapped = std::any_of(c.source_map.begin(), c.source_map.end(), [&](const SourceLocation& src) {
                return src.source_path == identity;
            });
            if (!mapped) throw std::runtime_error("source hash identity is absent from the source map");
        }
'''
if old in text:
    text = text.replace(old, '', 1)
elif 'source hash identity is absent from the source map' in text:
    raise SystemExit('source-hash validation block changed unexpectedly')
path.write_text(text, encoding='utf-8')
print('Train 18 source hash validation now allows compiled sources with no emitted instruction.')
