from pathlib import Path

p = Path("tests/source_diagnostic_tests.cpp")
text = p.read_text()
old = "void test_zzw_sequence() {"
new = "void test_zwj_sequence() {"
if old in text:
    if text.count(old) != 1:
        raise SystemExit("ZWJ diagnostic fixture: expected one misspelled declaration")
    p.write_text(text.replace(old, new, 1))
    print("applied: ZWJ diagnostic test declaration")
elif new in text:
    print("already applied: ZWJ diagnostic test declaration")
else:
    raise SystemExit("ZWJ diagnostic fixture declaration not found")
