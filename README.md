# Emojineer 🧑‍💻✨

**A ground-up emoji-native programming language.**

Emojineer is not emoji syntax painted over Python or JavaScript. `.emoji` source is lexed as Unicode grapheme clusters, parsed into an AST, compiled to Emojineer's own `EMJBC` bytecode, and executed by Emojineer's own VM.

```text
UTF-8 .emoji → grapheme lexer → parser → AST → EMJBC bytecode → Emojineer VM
```

## It runs now

```emoji
🐍 🍎 🔢 🟰 3

🔁 🍎 🔼 0
  📝 🍎
  ✏️ 🍎 🟰 🍎 ➖ 1
🏁

📝 📜Liftoff 🚀📜
```

Output:

```text
3
2
1
Liftoff 🚀
```

## Build

Requirements: C++20, CMake 3.20+, and ICU 70+ (`uc` + `i18n`).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Then:

```bash
./build/emojineer run examples/hello.emoji
./build/emojineer explain examples/countdown.emoji
./build/emojineer compile examples/countdown.emoji
./build/emojineer exec examples/countdown.emjbc
```

## Why ICU is here

Emoji are extended grapheme clusters, not reliably one code point. Emojineer normalizes token identity and deliberately ignores text-vs-emoji variation selectors while preserving meaningful ZWJ/modifier sequences. That prevents an editor from silently changing program semantics just because it rendered the same glyph differently.

See [`docs/LANGUAGE.md`](docs/LANGUAGE.md) for the v0.1 language contract and bytecode model.

## Status

Train 1 establishes the sovereign executable spine: Unicode token identity, lexer, parser, AST, compiler, bytecode serializer, VM, typed globals, arithmetic, control flow, I/O, explain mode, examples, and tests. Functions, collections, CER/custom tokens, package management, interop capabilities, LSP/editor support, and native code generation build on this spine.
