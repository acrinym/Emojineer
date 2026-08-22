# Emojineer Documentation

Emojineer is a sovereign emoji-native programming language. Its source is parsed into Emojineer's AST, linked across source or built-in standard modules, compiled to Emojineer's `EMJBC` bytecode, and executed by Emojineer's VM. It is not emoji syntax translated into Python, JavaScript, C++, or another host language.

## Start here

- [Language reference](LANGUAGE.md) — source model, tokens, grammar, values, functions, collections, control flow, modules, and standard-module imports.
- [Standard library](STDLIB.md) — built-in `std:math`, `std:arrays`, and `std:text` APIs and architecture.
- [CLI and toolchain](CLI.md) — build, run, check, format, lint, explain, REPL, compile, execute, dump, disassemble, standard-library listing, and `emji` project commands.
- [EMJBC bytecode](BYTECODE.md) — file format, compatibility versions, constants, function metadata, instructions, verifier limits, and VM execution model.
- [Modules and imports](MODULES.md) — multi-file source units, visibility, deterministic identity, loading order, cycles, project roots, and standard-module integration.
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

The compiler executable reports Emojineer **0.9**. Product Trains 1 through 9 are implemented on this train:

1. sovereign language core;
2. functions, recursion, and call frames;
3. Custom Emoji Registry;
4. first-class collections;
5. REPL and bytecode tooling;
6. canonical source formatting and diagnostics;
7. `emji` project workflow;
8. modules and imports;
9. native standard-library foundation.

The documentation describes implemented behavior unless a section is explicitly labeled as future work.

## File and module forms

- `.emoji` — Emojineer UTF-8 source.
- `.emjbc` — serialized Emojineer bytecode.
- `emojineer.toml` — strict project manifest.
- `emojineer.lock` — deterministic project lock metadata.
- CER `.json` files — optional custom emoji token packs that lower into existing Emojineer semantic token kinds.
- `std:<name>` — deterministic built-in standard module specifier resolved by the module linker.
