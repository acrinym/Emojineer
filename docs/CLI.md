# Emojineer CLI and Toolchain

Emojineer builds two command-line executables:

- `emojineer` - source, bytecode, formatting, REPL, standard-library, package-aware linking, and execution tools;
- `emji` - project, local dependency, lockfile, package-graph, and package-artifact workflow.

The current toolchain version is **0.13**.

## Build

Requirements:

- C++20 compiler;
- CMake 3.20+;
- ICU 70+ with `uc` and `i18n`.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## `emojineer` source and bytecode commands

```text
emojineer run program.emoji
emojineer check program.emoji
emojineer explain program.emoji
emojineer fmt program.emoji [-o formatted.emoji]
emojineer lint program.emoji
emojineer dump program.emoji
emojineer compile program.emoji [-o output.emjbc]
emojineer exec program.emjbc
emojineer disasm program.emjbc
emojineer stdlib
emojineer repl [--cer registry.json ...]
```

File compilation resolves local modules, declared `pkg:<dependency>/<module>.emoji` imports, and built-in `std:<module>` imports through the same package-aware linker. Package-qualified module identities remain checkout-portable and never encode absolute package roots.

`fmt` produces canonical source form. `lint` diagnoses departures from that form. `compile` writes sovereign `EMJBC`; `exec` and `disasm` read already-compiled bytecode. `stdlib` lists the built-in standard modules. The REPL remains single-source and shares its input stream with the VM for `📥`.

Source-oriented commands may accept one or more `--cer` Custom Emoji Registry packs where documented. CER packs extend lexical spellings into existing core semantic token kinds; they do not execute host-language code or define another runtime.

## `emji` project commands

### Initialize

```text
emji init my-project
emji init my-project --name signal_lab
```

Creates a strict `emojineer.toml`, deterministic `emojineer.lock`, and `src/main.emoji` starter source.

### Check

```text
emji check
emji check path/to/project
```

Validates the root manifest/entry, recursively resolves local/path dependencies, validates package-aware source graphs, and checks an existing lockfile for drift.

### Lock

```text
emji lock
emji lock path/to/project
```

Writes deterministic lockfile v2 metadata. Dependency records include version, checkout-relative resolved path, direct dependency names, and package content SHA-256.

### Show

```text
emji show
emji show path/to/project
```

Displays root package name, version, entry, canonical manifest hash, and direct dependency declarations.

### Tree

```text
emji tree
emji tree path/to/project
emji tree path/to/project --hashes
emji tree path/to/project --json
```

Resolves the real `PackageGraph` used by locking/linking. Human output labels packages `root`, `direct`, or `transitive`, shows checkout-relative paths and entries, and marks repeated shared-DAG nodes with `(shared)`. `--hashes` adds package content SHA-256. `--json` emits deterministic `emojineer.package-graph.v1` records with full hashes and no absolute checkout roots.

### Add/remove local dependencies

```text
emji add mathkit ../mathkit
emji add mathkit ../mathkit path/to/project
emji remove mathkit
emji remove mathkit path/to/project
```

Local dependency paths remain relative and first-class. Candidate graphs are validated before manifest changes are committed, and successful changes refresh canonical lock metadata.

## Immutable package artifacts

Train 13 adds a real distributable source-artifact substrate without pretending network transport exists.

### Pack

```text
emji pack
emji pack path/to/project
emji pack path/to/project -o dist/project.emjpkg
```

Creates deterministic `EMJPKG1` bytes. Without `-o`, output is `<project-root>/<name>-<version>.emjpkg`.

The package version must satisfy the registry artifact's strict SemVer 2.0 contract. The artifact includes canonical package metadata, the canonical manifest, and sorted package-owned `.emoji` source records. Resolved dependency-owned source trees are excluded using the same ownership model as `PackageGraph` content hashing.

Artifact output must use the `.emjpkg` extension.

### Inspect an artifact

```text
emji artifact package.emjpkg
```

Strictly parses/verifies the artifact, then prints:

- package name;
- version;
- entry source;
- source-file count;
- package `content-sha256`;
- whole `artifact-sha256`.

### Verify an artifact

```text
emji verify-artifact package.emjpkg
```

Returns success only after format bounds, canonical manifest metadata, source paths/order, per-source SHA-256, entry presence, package content identity, and trailing-byte checks all pass.

The verifier does not extract or execute source files.

## Artifact identity and cache contract

`content-sha256` is the existing package semantic/content identity over canonical manifest plus package-owned source.

`artifact-sha256` is SHA-256 over the exact `.emjpkg` bytes. Future downloads/cache entries are keyed by this exact artifact identity:

```text
<cache>/<package>/<version>/<artifact-sha256>.emjpkg
```

See [REGISTRY.md](REGISTRY.md) for the binary framing, trust boundary, SemVer range rules, and future transport contract.

## Current boundaries

The v0.13 toolchain has real local/path dependency resolution, package-qualified imports, deterministic graph inspection, and deterministic immutable package artifacts with checksum verification and SemVer selection primitives.

It still has **no remote registry client**, network package retrieval, remote dependency manifest syntax, remote version locking, authenticated publication, `emji publish`, or remote `emji add`. Those belong to the next train and must use the v0.13 artifact/content identities rather than inventing a second distribution model.

The language runtime itself still receives no ambient filesystem, network, process, clock, randomness, shell, or FFI authority.
