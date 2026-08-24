# `emji` Projects and Local Dependencies

`emji` is Emojineer's project/package workflow. In v0.13 it manages deterministic local/path dependency graphs, connects them to the source linker, exposes the resolved graph for humans/tooling, and can package one package's owned source into a deterministic immutable `.emjpkg` artifact without pretending a remote registry exists.

## Project layout

A newly initialized project looks like:

```text
my-project/
├── emojineer.toml
├── emojineer.lock
└── src/
    └── main.emoji
```

Initialize one with:

```text
emji init my-project
emji init my-project --name signal_lab
```

## Manifest

`emojineer.toml` is intentionally strict:

```toml
[package]
name = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"

[dependencies]
mathkit = "../mathkit"
textkit = "vendor/textkit"
```

Required package fields remain:

- `name` — non-empty ASCII letters/digits plus `-` and `_`;
- `version` — semantic-version-shaped text for ordinary local project use;
- `entry` — relative portable path to a `.emoji` source file.

Distribution artifacts use the stricter SemVer 2.0 parser defined in [REGISTRY.md](REGISTRY.md), so `emji pack` rejects a package version that cannot be represented by the registry/version-selection contract.

Dependency keys use the same portable package-name rules. Dependency values are relative filesystem paths. `..` is allowed for dependencies so sibling packages are possible; absolute paths and backslash syntax are rejected so manifests remain checkout-portable.

A dependency key must match the target package's `[package].name`. Duplicate dependency names, missing manifests, cycles, and one package name resolving to multiple local roots are rejected.

## Add and remove dependencies

```text
emji add mathkit ../mathkit
emji remove mathkit
emji add mathkit ../mathkit path/to/app
emji remove mathkit path/to/app
```

`add` validates the candidate graph before writing the manifest, then rewrites the manifest canonically and refreshes the lockfile. `remove` does the same after deleting the named dependency. A failed add does not leave the candidate dependency declaration behind.

This dependency workflow still supports local/path packages only. There is no fake network registry, no `publish`, and no remote version solver.

## Recursive package graph

Dependency resolution is recursive. Each package resolves dependency paths relative to its own package root.

Resolution enforces deterministic package-name ordering, one resolved root per package name, dependency key/target name agreement, cycle diagnostics, and required dependency manifests.

Package content identity uses SHA-256 over a framed representation of the package's canonical manifest plus its owned `.emoji` source files. Dependency-owned source trees are excluded, including transitive/nested resolved roots. That ownership contract is reused by `.emjpkg` packaging.

## Inspect the resolved graph

```text
emji tree
emji tree path/to/project
emji tree path/to/project --hashes
emji tree path/to/project --json
```

Human output reports package name/version, `root` / `direct` / `transitive` relation, checkout-relative path, entry source, and optionally content SHA-256. Shared DAG nodes are shown again with `(shared)` but are not recursively expanded twice.

`--json` emits deterministic `emojineer.package-graph.v1` data with no timestamps or absolute checkout roots.

## Importing package source

A declared direct dependency can be imported with:

```emoji
🧩 🚀
🔗 📜pkg:mathkit/src/main.emoji📜
```

Transitive dependencies do not become ambient imports. Relative imports stay inside the importing package's owned root, and `pkg:` paths cannot tunnel through one resolved package into another nested package.

## Immutable package artifacts

Train 13 packages a single resolved package's owned source without absorbing dependency-owned source:

```text
emji pack
emji pack path/to/project
emji pack path/to/project -o dist/project.emjpkg
```

The artifact contains the canonical manifest, declared package identity, entry, sorted package-owned `.emoji` records, per-source SHA-256 values, and the package `content-sha256` already defined by `PackageGraph`.

The complete serialized bytes have a separate `artifact-sha256`. This is the future transport/cache identity. Equivalent checkouts with identical package-owned inputs produce byte-identical artifacts.

Inspect or verify one with:

```text
emji artifact package.emjpkg
emji verify-artifact package.emjpkg
```

Verification is strict and non-extracting: malformed framing, noncanonical manifest metadata, unsafe/noncanonical source paths, missing entry source, source checksum mismatch, package content-identity mismatch, or trailing bytes are rejected.

See [REGISTRY.md](REGISTRY.md) for the complete `EMJPKG1` and SemVer contracts.

## Project validation

```text
emji check
emji check path/to/project
```

Validation includes strict manifest parsing, root entry-file existence/package-aware compilation, recursive local dependency resolution, dependency source-graph validation, and deterministic lockfile freshness when a lock exists.

Package graph failures and source-module cycle failures remain distinct layers.

## Deterministic lockfile v2

```text
emji lock
emji lock path/to/project
```

v0.13 continues to write local/path lock format 2:

```text
lock_version = 2
manifest_hash = "..."
package = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"
dependency_count = 1

[[dependency]]
name = "mathkit"
version = "0.4.0"
path = "../mathkit"
content_sha256 = "..."
dependencies = ""
```

Records are deterministic, sorted, checkout-relative, and timestamp-free. `manifest_hash` remains FNV-1a-64 as a drift marker; dependency content is pinned by SHA-256.

Remote registry locking is **not** encoded into this format yet. The next train must add registry identity, requirement, selected version, content SHA-256, and artifact SHA-256 together rather than overloading local-path records ambiguously.

## Relationship to modules and artifacts

- `emojineer.toml` defines package metadata and local dependency edges;
- `emji tree` inspects the resolved `PackageGraph`;
- `🧩`, `🔗`, and `📤` define source-module relationships;
- `.emjpkg` serializes one package's canonical manifest and package-owned source;
- `content-sha256` identifies package meaning;
- `artifact-sha256` identifies exact serialized artifact bytes.

None of these layers grants ambient access to transitive packages, arbitrary checkout files, or host network/filesystem capabilities.
