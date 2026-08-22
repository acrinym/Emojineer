# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Language reference](LANGUAGE.md) — source model, tokens, grammar, values, functions, collections, control flow, and modules.
- [CLI and toolchain](CLI.md) — build, run, check, format, lint, explain, REPL, compile, execute, dump, disassemble, and `emji` project commands.
- [EMJBC bytecode](BYTECODE.md) — file format, compatibility versions, constants, function metadata, instructions, verifier limits, and VM execution model.
- [Modules and imports](MODULES.md) — multi-file source units, visibility, deterministic identity, loading order, cycles, and project roots.
- [`emji` projects](PROJECTS.md) — manifest, lockfile, initialization, project validation, and the current local package foundation.

## Focused references

- [Functions](FUNCTIONS.md)
- [Collections](COLLECTIONS.md)
- [Custom Emoji Registry](CER.md)
- [Formatter and lint](SOURCE_TOOLING.md)
- [REPL and bytecode tooling](TOOLING.md)
- [Design provenance](PROVENANCE.md)
- [Product roadmap](ROADMAP.md)

## Current implemented language level

The compiler executable reports Emojineer **0.8**. Product Trains 1 through 8 are landed:

1. sovereign language core;
2. functions, recursion, and call frames;
3. Custom Emoji Registry;
4. first-class collections;
5. REPL and bytecode tooling;
6. canonical source formatting and diagnostics;
7. `emji` project workflow;
8. modules and imports.

The documentation describes implemented behavior unless a section is explicitly labeled as future work.

## File types

- `.emoji` — Emojineer UTF-8 source.
- `.emjbc` — serialized Emojineer bytecode.
- `emojineer.toml` — strict project manifest.
- `emojineer.lock` — deterministic project lock metadata.
- CER `.json` files — optional custom emoji token packs that lower into existing Emojineer semantic token kinds.
