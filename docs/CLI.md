# Emojineer CLI and Toolchain

Emojineer 0.19 builds three native C++ executables:

- `emojineer` - source, bytecode, formatting, REPL, standard-library, execution, and source-level debugging;
- `emji` - project/package/registry workflow, authenticated publication, remote dependency sync, and package discovery;
- `emojineer-lsp` - JSON-RPC/LSP server over the sovereign compiler/module/package model.

## Build and qualify

Requirements: C++20, CMake 3.20+, ICU 70+. libcurl is optional for HTTPS registry reads/publication/discovery; file registries remain fully available without it.

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

## `emojineer`

```text
emojineer repl [--cer registry.json ...]
emojineer stdlib
emojineer debug <source-or-project>
emojineer <run|check|explain|dump|lint> <file.emoji> [--cer registry.json ...]
emojineer fmt <file.emoji> [-o file.emoji] [--cer registry.json ...]
emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]
emojineer <exec|disasm> <file.emjbc>
```

File/project compilation uses the normal package-aware module linker. The debugger runs over the production compiler/EMJBC/VM, not a second evaluator.

## Core `emji` project workflow

```text
emji init <directory> [--name project_name]
emji check [directory]
emji lock [directory]
emji show [directory]
emji tree [directory] [--hashes] [--json]
emji sync [directory] [--offline] [--cache directory]
emji add <package_name> <relative_path> [directory]
emji add <package_name> <requirement> --registry <endpoint> [--registry-name <alias>] [directory]
emji remove <package_name> [directory]
```

Registry dependencies are explicit manifest state. Lock v3 records the selected version, registry identity/endpoint, requirement, content/artifact SHA-256, materialized store path, and dependency edges. Ordinary compilation/execution resolves remote packages from verified lock/store state and does not initiate registry access.

## Immutable artifacts and registry exchange

```text
emji pack [directory] [-o package.emjpkg]
emji artifact <package.emjpkg>
emji verify-artifact <package.emjpkg>
emji registry-init <directory> --id <registry_id>
emji registry-info --registry <endpoint>
emji versions <package_name> --registry <endpoint>
emji fetch <package_name> <requirement> --registry <endpoint> [--cache directory]
```

`EMJREGPKG1` indexes bind exact versions to package content SHA-256 and whole-artifact SHA-256. Fetch verifies both identities before cache admission and re-verifies cache hits.

## Publication

File registries are credential-free:

```text
emji publish [directory] --registry ./registry
```

HTTPS publication uses authenticated `emjpub1` and requires `EMOJINEER_TOKEN` plus a non-secret namespace:

```text
EMOJINEER_TOKEN='…' emji publish [directory] \
  --registry https://registry.example/api \
  --namespace namespace_name \
  [--receipt receipt.json]
```

The client verifies registry identity before sending credentials, disables redirects, verifies TLS peer/host, uploads the actual immutable `.emjpkg`, verifies the strict receipt, and persists it atomically. Credentials never enter manifests, locks, artifacts, source, or receipts.

## Package search and discovery

```text
emji search <query> --registry <endpoint> [--include-prerelease] [--json]
emji package-info <package_name> --registry <endpoint> [--include-prerelease] [--json]
emji dependents <package_name> --registry <endpoint> [--include-prerelease] [--json]
emji discovery-index --registry <endpoint>
```

Search uses canonical discoverable metadata: package name, entry path, and direct dependency names. `*` lists all eligible packages. Multiple whitespace-separated query terms use deterministic AND semantics.

By default discovery selects the highest stable SemVer for each package. `--include-prerelease` allows prerelease versions and selects the highest SemVer across all releases.

`package-info` reports the selected release, all discoverable versions, entry, direct dependencies, and immutable content/artifact identities. `dependents` performs direct reverse-dependency queries over one selected release per package.

File registries derive discovery from their real package indexes and verified artifacts. HTTPS registries serve bounded canonical `EMJREGDISC1` metadata at `v1/discovery.index`, whose registry ID must match `EMJREGISTRY1`. `discovery-index` emits the canonical wire representation.

JSON output schemas are `emojineer.registry-search.v1`, `emojineer.registry-package-info.v1`, and `emojineer.registry-dependents.v1`.

## LSP

`emojineer-lsp` speaks JSON-RPC/LSP over stdio and supports document lifecycle, diagnostics, formatting, completion, hover, definition/references, and document/workspace symbols with correct UTF-16 position translation. Editor requests use already-verified package state and do not perform registry networking.

## Authority boundary

Package-manager networking, authenticated publication, and discovery are tooling authority. They do not grant Emojineer programs or the VM ambient network, filesystem, process, clock, randomness, credential, or host-resource authority.
