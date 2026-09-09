# Emojineer CLI and Toolchain

Emojineer 0.20 builds three native C++ executables:

- `emojineer` - source, bytecode, formatting, REPL, capability inspection, execution, and source-level debugging;
- `emji` - project/package/registry workflow, authenticated publication, remote dependency sync, and package discovery;
- `emojineer-lsp` - JSON-RPC/LSP server over the sovereign compiler/module/package model.

## Build and qualify

Requirements: C++20, CMake 3.20+, ICU 70+. libcurl is optional for HTTPS registry reads/publication/discovery and the explicitly granted `🌐` runtime facility.

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
emojineer repl [--cer registry.json ...] [execution-policy]
emojineer stdlib
emojineer debug <source-or-project> [--cer registry.json ...] [execution-policy]
emojineer run <file.emoji> [--cer registry.json ...] [execution-policy]
emojineer <check|explain|dump|lint> <file.emoji> [--cer registry.json ...]
emojineer fmt <file.emoji> [-o file.emoji] [--cer registry.json ...]
emojineer compile <file.emoji> [-o file.emjbc] [--cer registry.json ...]
emojineer exec <file.emjbc> [execution-policy]
emojineer disasm <file.emjbc>
emojineer capabilities <file.emoji|file.emjbc> [--cer registry.json ...]
```

File/project compilation uses the normal package-aware module linker. The debugger and REPL execute through the production VM, not alternate evaluators.

### Execution policy

```text
--grant <filesystem|network|process|clock|random|host|all>
--sandbox
--deterministic
--seed <u64>
--clock-ms <i64>
```

`--grant` is repeatable. Runtime grants are accepted only by `run`, `exec`, `debug`, and `repl`. Compile/check/format/lint/disassembly/capability inspection, LSP work, and `emji` operations do not inherit program-runtime grants.

Default execution has no Train 20 host grants. `--sandbox` is hard zero-host-capability mode and rejects grants. `--deterministic` accepts only `clock` and `random`; `--seed` and `--clock-ms` configure their reproducible VM-local state. See [CAPABILITIES.md](CAPABILITIES.md).

`capabilities` compiles source or reads EMJBC and reports the exact whole-program capability mask without executing it.

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

Registry dependencies are explicit manifest state. Lock v3 records selected version, registry provenance, immutable hashes, materialized store path, and dependency edges. Ordinary compile/run resolves remote packages from verified lock/store state and does not initiate registry access.

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

Authenticated HTTPS publication uses `emjpub1`, requires `EMOJINEER_TOKEN`, verifies registry identity before authorization, disables redirects, verifies TLS, uploads the exact immutable artifact, and verifies/persists the returned receipt.

```text
EMOJINEER_TOKEN='…' emji publish [directory] \
  --registry https://registry.example/api \
  --namespace namespace_name \
  [--receipt receipt.json]
```

Credentials never enter manifests, locks, artifacts, source, or receipts.

## Package search and discovery

```text
emji search <query> --registry <endpoint> [--include-prerelease] [--json]
emji package-info <package_name> --registry <endpoint> [--include-prerelease] [--json]
emji dependents <package_name> --registry <endpoint> [--include-prerelease] [--json]
emji discovery-index --registry <endpoint>
```

Discovery uses canonical `EMJREGDISC1` metadata. File registries derive it from real indexes plus verified artifacts; HTTPS registries expose bounded `v1/discovery.index` metadata whose registry ID must match `EMJREGISTRY1`. Stable releases are selected by default; `--include-prerelease` opts into prerelease selection.

JSON schemas are `emojineer.registry-search.v1`, `emojineer.registry-package-info.v1`, and `emojineer.registry-dependents.v1`.

## LSP

`emojineer-lsp` speaks JSON-RPC/LSP over stdio and supports document lifecycle, diagnostics, formatting, completion, hover, definition/references, and document/workspace symbols with UTF-16 position translation. Editor requests use already-verified package state and do not perform registry networking or receive runtime host grants.

## Authority boundary

There are two deliberately separate authority planes:

1. `emji` package-manager authority may perform explicit registry reads/publication without granting anything to programs.
2. Emojineer VM authority starts empty and receives only explicit Train 20 execution grants.

Neither plane implicitly inherits the other. Imported dependencies contribute to the linked program's required capability mask, and the VM checks the entire mask before the first instruction executes.
