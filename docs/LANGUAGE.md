# Emojineer Language v0.1

Emojineer is an emoji-native programming language. The source is not translated to Python, JavaScript, C#, or C++. The current compiler is implemented in C++20, but it lowers Emojineer syntax into the project's own `EMJBC` bytecode, which runs on the project's own stack VM.

## Source and Unicode contract

Source files use UTF-8 and the `.emoji` extension.

The lexer segments source using Unicode extended grapheme clusters through ICU. Token identity is NFC-normalized. U+FE0E and U+FE0F presentation selectors are removed before token lookup so visually equivalent text/emoji presentation forms do not change program meaning. ZWJ sequences and skin-tone modifiers are preserved. This is deliberate: modifier-bearing graphemes remain available as distinct future tokens and identifiers.

Whitespace separates tokens. Newlines terminate simple statements. Blocks are closed explicitly with `🏁`.

ASCII digits are permitted as numeric literal data, and arbitrary Unicode text is permitted inside `📜...📜` string literals. Outside literals, non-whitespace syntax is emoji-native.

## Core v0.1 token map

| Glyph | Meaning |
| --- | --- |
| `🐍` | declare variable |
| `✏️` | assign variable |
| `📝` | print |
| `🤔` | if |
| `🙅` | else |
| `🔁` | while |
| `🏁` | end block |
| `📥` | read a line of input |
| `✅` / `❌` | booleans |
| `🔢` / `🔤` / `🎯` | optional number/text/boolean declaration type |
| `➕` `➖` `✖️` `➗` `🪄` | arithmetic; `➕` also concatenates text |
| `🟰` | assignment separator or equality comparison |
| `🔽` / `🔼` | less-than / greater-than |
| `🚫` | boolean NOT |
| `🫴` / `🤲` | group an expression |
| `📜...📜` | text literal |
| `💭` | line comment |

Any non-reserved extended emoji grapheme may be used as a variable identifier. For example, `🍎`, `👤`, and a single ZWJ family grapheme are valid and have stable canonical identities.

## Grammar

```text
program      := (NEWLINE | statement)* EOF
statement    := var_decl | assignment | print | if_stmt | while_stmt
var_decl     := 🐍 IDENTIFIER type? 🟰 expression LINE_END
assignment   := ✏️ IDENTIFIER 🟰 expression LINE_END
print        := 📝 expression LINE_END
if_stmt      := 🤔 expression LINE_END block (🙅 LINE_END block)? 🏁 LINE_END
while_stmt   := 🔁 expression LINE_END block 🏁 LINE_END
block        := statement*
type         := 🔢 | 🔤 | 🎯
expression   := equality
equality     := comparison (🟰 comparison)*
comparison   := term ((🔽 | 🔼) term)*
term         := factor ((➕ | ➖) factor)*
factor       := unary ((✖️ | ➗ | 🪄) unary)*
unary        := (➖ | 🚫) unary | primary
primary      := NUMBER | STRING | ✅ | ❌ | IDENTIFIER | 📥 | 🫴 expression 🤲
```

Conditions are strict booleans. Arithmetic is strict numeric except `➕`, which accepts either two numbers or two strings. Typed declarations are checked by bytecode instructions, and later assignments preserve the declared type when the declaration is visible to the compiler.

## Bytecode and VM

`.emoji` source compiles to `.emjbc` files with the `EMJBC` magic header and bytecode format version 1. Multi-byte fields are explicitly little-endian, numeric constants are serialized as IEEE-754 binary64 bit patterns, and readers enforce bounded constant/string/instruction sizes before allocation. The bytecode contains a constant pool plus fixed-width instructions. Implemented opcodes cover constants, globals, runtime type assertions, arithmetic, comparison, input/output, jumps, and halt.

The VM has an execution-fuel counter so runaway loops terminate instead of consuming a host indefinitely. v0.1 exposes only stdin/stdout. It has no implicit filesystem, network, shell, FFI, or host-language escape hatch.

## Explain mode

`emojineer explain file.emoji` asks the lexer to render every meaningful token in plain English, including canonical code points for identifiers. This is the first accessibility/debugging surface and is intentionally part of the core compiler rather than an editor-only feature.

## CLI

```text
emojineer run program.emoji
emojineer check program.emoji
emojineer explain program.emoji
emojineer compile program.emoji
emojineer exec program.emjbc
```

## Next language trains

The existing design calls for functions, arrays/collections, user structures/interfaces, modules, errors, pattern matching, a standard library, package/version management, a Custom Emoji Registry (CER), modifier-aware token vocabulary, safe polyglot capabilities, a REPL/debugger/LSP, and later native/LLVM backends. Those extend this bytecode/VM spine rather than replacing it.
