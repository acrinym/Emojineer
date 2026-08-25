# Emojineer CLI and Toolchain

Emojineer builds two command-line executables:

- `emojineer` - source, bytecode, formatting, REPL, standard-library, package-aware linking, and execution tools;
- `emji` - project, local dependency, lockfile, package-graph, artifact, and registry workflow.

The current toolchain version is **0.14**.

## Build

Requirements:

- C++20 compiler;
- CMake 3.20+;
- ICU 70+ with `uc` and `i18n`;
- optional libcurl for HTTPS registry reads.

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

### Pack

```text
emji pack
emji pack path/to/project
emji pack path/to/project -o dist/project.emjpkg
```

Creates deterministic `EMJPKG1` bytes. Without `-o`, output is `<project-root>/<name>-<version>.emjpkg`.

### Inspect

```text
emji artifact package.emjpkg
```

Strictly parses/verifies the artifact, then prints package identity, file count, package `content-sha256`, and whole `artifact-sha256`.

### Verify

```text
emji verify-artifact package.emjpkg
```

Returns success only after format bounds, canonical manifest metadata, source paths/order, per-source SHA-256, entry presence, package content identity, and trailing-byte checks pass.

## Registry commands

### Initialize a local registry

```text
emji registry-init ./registry --id local.dev
```

Creates the `EMJREGISTRY1` descriptor and canonical `v1/packages` / `v1/artifacts/sha256` layout. Repeating initialization with the same ID is idempotent. Reinitializing the same registry with a different ID is rejected.

### Inspect registry identity

```text
emji registry-info --registry ./registry
emji registry-info --registry https://registry.example/api
```

Prints registry ID, canonical endpoint, selected transport, and whether HTTPS support was compiled into the current build.

Network endpoints must use HTTPS. Plain HTTP is rejected.

### Browse package versions

```text
emji versions mathkit --registry ./registry
```

Loads the bounded `EMJREGPKG1` package index, verifies that its registry/package identity matches the request, and prints immutable version records with package content SHA-256 and artifact SHA-256.

### Publish an immutable package version

```text
emji publish path/to/package --registry ./registry
```

Publication is currently supported for writable local/file registries. It builds and verifies the package artifact, stores it under its exact artifact SHA-256, and adds the version/hash tuple to the package index.

Publishing the exact same package/version again is idempotent. Publishing different content under an existing package/version is rejected.

HTTPS publication is intentionally not available until an authenticated immutable-upload protocol and authorization model are defined.

### Resolve and fetch an artifact

```text
emji fetch mathkit '^1.2.0' --registry ./registry
emji fetch mathkit '~1.4.0' --registry https://registry.example/api
emji fetch mathkit '^1.2.0' --registry ./registry --cache ./cache
```

`fetch`:

1. verifies registry discovery identity;
2. loads the package index;
3. selects the highest matching version using the Train 13 SemVer engine;
4. retrieves the exact artifact addressed by the index;
5. verifies package name, exact version, package content SHA-256, and artifact SHA-256;
6. admits the artifact to a registry-scoped content-addressed cache only after verification.

A verified cache hit avoids retrieval. A malformed or mismatched cache entry is discarded and fetched again.

If `--cache` is omitted, platform cache conventions are used. See [REGISTRY.md](REGISTRY.md) for the exact cache and trust model.

## HTTPS behavior

CMake detects libcurl optionally. When present, HTTPS registry reads use certificate/host verification, disallow redirects, enforce bounded response buffers, and use connection/overall request timeouts. A build without libcurl still supports the complete local registry workflow and emits a clear error for HTTPS endpoints.

Registry support belongs to `emji`; it does not grant ambient network authority to Emojineer programs or the VM.

## Current boundary

The v0.14 toolchain now has a real artifact exchange protocol, but remote registry packages are **not yet project dependencies**. `emojineer.toml` and lockfile v2 still describe local/path dependencies only.

The next package train must add registry dependency declarations, recursive remote resolution/materialization, registry provenance in deterministic locks, and package-aware linking against the verified materialized graph before remote `emji add` can be considered real.
