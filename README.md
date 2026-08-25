# Emojineer 🧑‍💻✨

**A sovereign, ground-up emoji-native programming language.**

Emojineer is not emoji syntax painted over Python, JavaScript, C++, or another host language. `.emoji` source is tokenized as Unicode grapheme clusters, parsed into an Emojineer AST, linked across source, package, and standard modules when needed, compiled into Emojineer's own `EMJBC` bytecode, and executed by Emojineer's own VM.

```text
UTF-8 .emoji → grapheme lexer → parser → AST → package-aware module linker → EMJBC → Emojineer VM
```

Current language/toolchain version: **0.17**.

## What works now

Product Trains 1 through 14 provide:

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
- deterministic `emji tree` package graph inspection with root/direct/transitive classification, optional SHA-256 identities, shared-DAG markers, and checkout-portable JSON;
- deterministic immutable `.emjpkg` source artifacts, strict checksum verification, content-addressed artifact identity, and full SemVer 2.0 requirement/selection primitives;
- real registry identity/discovery, immutable local-registry publication, package-version indexes, verified artifact fetching, content-addressed cache reuse/repair, and optional HTTPS read transport through libcurl.

## Package workflow

```bash
./build/emji init my-project
./build/emji check my-project
./build/emji lock my-project
./build/emji show my-project
./build/emji tree my-project --hashes
./build/emji tree my-project --json
./build/emji pack my-project
./build/emji artifact my-project/my-project-0.1.0.emjpkg
./build/emji verify-artifact my-project/my-project-0.1.0.emjpkg
```

`.emjpkg` artifacts contain only package-owned `.emoji` sources, the canonical manifest, per-source SHA-256 values, and the package content SHA-256. The complete serialized artifact has a separate SHA-256 used as its immutable transport/cache identity. Equivalent checkouts produce identical artifact bytes when their package-owned inputs are identical.

## Registry workflow

A local registry is a real implementation of the same registry protocol used by the read client:

```bash
./build/emji registry-init ./registry --id local.dev
./build/emji publish my-project --registry ./registry
./build/emji registry-info --registry ./registry
./build/emji versions my-project --registry ./registry
./build/emji fetch my-project '^0.1.0' --registry ./registry
```

Registry package indexes bind each immutable package version to both its package `content-sha256` and exact `.emjpkg` `artifact-sha256`. Fetch resolves the requested SemVer range, verifies the complete artifact against the index, then admits it to a registry-scoped content-addressed cache. Corrupt cache entries are discarded and fetched again.

Network registry endpoints must use `https://`. HTTPS reads are enabled when Emojineer is built with libcurl; local file registries remain available without it. Plain HTTP is rejected. HTTPS publication is intentionally not exposed yet because an authenticated immutable upload contract has not been defined.

Registry-fetched artifacts are also **not yet written into `emojineer.toml`**. Remote dependency declarations and lockfile provenance need to land together so a future remote `emji add` remains deterministic and reproducible instead of becoming a disguised mutable path dependency.

## Package import example

Given a declared local dependency:

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

## Build

Requirements: C++20, CMake 3.20+, and ICU 70+ (`uc` + `i18n`). libcurl is optional and enables HTTPS registry reads.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use the language toolchain

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

## Documentation

Start with **[`docs/README.md`](docs/README.md)**.

Key references:

- [`docs/LANGUAGE.md`](docs/LANGUAGE.md) — current language reference and grammar;
- [`docs/STDLIB.md`](docs/STDLIB.md) — native standard-library modules;
- [`docs/BYTECODE.md`](docs/BYTECODE.md) — EMJBC v1/v2/v3 format and VM contract;
- [`docs/CLI.md`](docs/CLI.md) — full command-line/toolchain guide;
- [`docs/MODULES.md`](docs/MODULES.md) — local, package, and standard module/import/export semantics;
- [`docs/PROJECTS.md`](docs/PROJECTS.md) — `emji` projects, local dependencies, package imports, graph inspection, and lockfiles;
- [`docs/REGISTRY.md`](docs/REGISTRY.md) — immutable artifacts, registry protocol, publishing/fetching, cache identity, and transport boundaries;
- [`docs/CER.md`](docs/CER.md) — Custom Emoji Registry;
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — next product trains.

## Why ICU is here

Emoji are extended grapheme clusters, not reliably one Unicode code point. Emojineer deliberately normalizes canonical token identity while preserving meaningful ZWJ and modifier sequences. A source editor should not silently change program meaning merely because it chose a different emoji presentation selector.

## Project direction

The next package frontier is **remote dependency integration** over the v0.14 registry transport: registry requirements in manifests, recursive registry dependency resolution, registry provenance in deterministic locks, materialization into a verified package store, and only then real remote `emji add`. Authenticated HTTPS publication remains a separate trust-boundary step. The longer roadmap continues into language-server/editor tooling, debugging, capability-gated host facilities, WASM/HIL interop, low-level ABI work, and later native compilation.
