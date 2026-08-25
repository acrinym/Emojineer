# Emojineer 🧑‍💻✨

**A sovereign, ground-up emoji-native programming language.**

Emojineer is not emoji syntax painted over Python, JavaScript, C++, or another host language. `.emoji` source is tokenized as Unicode grapheme clusters, parsed into an Emojineer AST, linked across source, package, and standard modules when needed, compiled into Emojineer's own `EMJBC` bytecode, and executed by Emojineer's own VM.

```text
UTF-8 .emoji → grapheme lexer → parser → AST → package-aware module linker → EMJBC → Emojineer VM
```

Current language/toolchain version: **0.12**.

## What works now

Product Trains 1 through 12 provide:

- Unicode/grapheme-aware emoji-native syntax and canonical token identity;
- variables, optional runtime declaration types, arithmetic, comparisons, booleans, input/output;
- `🤔` / `🙅` conditionals and `🔁` loops;
- `🛠️` functions, parameters, locals, recursion, forward references, and `📦` return;
- `📚` arrays, nested collections, structural equality, indexing, length, append, and replacement;
- Custom Emoji Registry packs with multi-grapheme longest-match tokens;
- sovereign `EMJBC` bytecode, verifier, VM execution fuel, dump/disassembly tooling;
- buffered REPL;
- canonical formatter and linter;
- deterministic multi-file `🧩` modules with local `🔗` imports and explicit `📤` exports;
- native standard modules imported as `std:math`, `std:arrays`, and `std:text`, implemented in Emojineer source and compiled by the normal toolchain;
- `emji` local/path dependency manifests, recursive package resolution, deterministic lockfile v2, and SHA-256 package content identity;
- explicit `pkg:<dependency>/<module>.emoji` imports through declared direct dependencies, with package-boundary enforcement and checkout-portable linked identities;
- deterministic `emji tree` package graph inspection with root/direct/transitive classification, optional SHA-256 identities, shared-DAG markers, and checkout-portable JSON output for tooling.

## A small program

```emoji
🐍 🍎 🔢 🟰 3

🔁 🍎 🔼 0
    📝 🍎
    ✏️ 🍎 🟰 🍎 ➖ 1
🏁

📝 📜Liftoff 🚀📜
```

## Standard library example

```emoji
🧩 🚀
🔗 📜std:math📜
🔗 📜std:text📜

📝 🧭 🫴 ➖ 9 🤲
📝 🔂 🫴 📜ha📜 3 🤲
```

Output:

```text
9
hahaha
```

List the built-in standard modules with:

```bash
./build/emojineer stdlib
```

See [`docs/STDLIB.md`](docs/STDLIB.md) for the current API.

## Package import example

Given a declared dependency:

```toml
[dependencies]
mathkit = "../mathkit"
```

import one of its modules explicitly:

```emoji
🧩 🚀
🔗 📜pkg:mathkit/src/main.emoji📜
```

Only the current package's declared **direct** dependencies are available through `pkg:`. Transitive packages do not become ambient imports, and normal relative imports cannot cross into dependency-owned source trees.

## Inspect the package graph

```bash
./build/emji tree my-project
./build/emji tree my-project --hashes
./build/emji tree my-project --json
```

Human output labels packages as `root`, `direct`, or `transitive`, shows checkout-relative paths and entries, and marks repeated nodes in shared dependency DAGs. `--hashes` adds each package's full SHA-256 content identity. `--json` emits the deterministic `emojineer.package-graph.v1` schema with full hashes and no absolute checkout paths.

## Build

Requirements: C++20, CMake 3.20+, and ICU 70+ (`uc` + `i18n`).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use the toolchain

```bash
./build/emojineer run examples/hello.emoji
./build/emojineer run examples/modules/main.emoji
./build/emojineer run examples/stdlib.emoji
./build/emojineer stdlib
./build/emojineer check examples/modules/main.emoji
./build/emojineer explain examples/countdown.emoji
./build/emojineer fmt examples/countdown.emoji
./build/emojineer lint examples/countdown.emoji
./build/emojineer compile examples/countdown.emoji
./build/emojineer exec examples/countdown.emjbc
./build/emojineer dump examples/collections.emoji
./build/emojineer repl
```

Project and local dependency workflow:

```bash
./build/emji init my-project
./build/emji check my-project
./build/emji lock my-project
./build/emji show my-project
./build/emji tree my-project
./build/emji tree my-project --hashes
./build/emji tree my-project --json
./build/emji add mathkit ../mathkit my-project
./build/emji remove mathkit my-project
```

Local/path dependencies are real package metadata and lock graph entries in v0.12, and the file compiler uses that graph when module syntax requires package-aware linking. Package access is explicit through `pkg:` coordinates rather than filesystem escapes. Graph inspection is derived from that same resolver rather than a second metadata model.

## Documentation

Start with **[`docs/README.md`](docs/README.md)**.

Key references:

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — current language reference and grammar;
- [`docs/STDLIB.md`](docs/STDLIB.md) — native standard-library modules;
- [`docs/BYTECODE.md`](docs/BYTECODE.md) — EMJBC v1/v2/v3 format and VM contract;
- [`docs/CLI.md`](docs/CLI.md) — full command-line/toolchain guide;
- [`docs/MODULES.md`](docs/MODULES.md) — local, package, and standard module/import/export semantics;
- [`docs/PROJECTS.md`](docs/PROJECTS.md) — `emji` projects, local dependencies, package imports, graph inspection, and lockfiles;
- [`docs/CER.md`](docs/CER.md) — Custom Emoji Registry;
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — next product trains.

## Why ICU is here

Emoji are extended grapheme clusters, not reliably one Unicode code point. Emojineer deliberately normalizes canonical token identity while preserving meaningful ZWJ and modifier sequences. A source editor should not silently change program meaning merely because it chose a different emoji presentation selector.

## Project direction

With package resolution, package-qualified imports, and deterministic graph inspection connected, the next major package frontier is a **real registry/distribution protocol**: artifact identity, cryptographic integrity, reproducible version selection, retrieval/cache semantics, and only then remote add/publish behavior. The longer roadmap continues into language-server/editor tooling, debugging, capability-gated host facilities, WASM/HIL interop, low-level ABI work, and later native compilation.
