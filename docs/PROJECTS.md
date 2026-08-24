# `emji` Projects and Local Dependencies

`emji` is Emojineer's project/package workflow. In v0.10 it manages deterministic local/path dependency graphs without pretending a remote registry exists.

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
- `version` — semantic-version-shaped text such as `0.1.0`;
- `entry` — relative portable path to a `.emoji` source file.

Dependency keys use the same portable package-name rules. Dependency values are relative filesystem paths. `..` is allowed for dependencies so sibling packages are possible; absolute paths and backslash syntax are rejected so manifests remain checkout-portable.

A dependency key must match the target package's `[package].name`. Duplicate dependency names, missing manifests, cycles, and one package name resolving to multiple local roots are rejected.

Unknown keys and unsupported manifest sections are rejected rather than silently ignored.

## Add and remove dependencies

From a project directory:

```text
emji add mathkit ../mathkit
emji remove mathkit
```

You may supply the project directory explicitly as the final argument:

```text
emji add mathkit ../mathkit path/to/app
emji remove mathkit path/to/app
```

`add` validates the candidate graph before writing the manifest, then rewrites the manifest canonically and refreshes the lockfile. `remove` does the same after deleting the named dependency. A failed add does not leave the candidate dependency declaration behind.

This train supports local/path packages only. There is no fake network registry, no `publish`, and no remote version solver.

## Recursive package graph

Dependency resolution is recursive. Each package resolves dependency paths relative to its own package root, so a package may itself have local dependencies.

Resolution enforces:

- deterministic package-name ordering;
- one resolved root per package name;
- dependency key/target package-name agreement;
- cycle detection with the package chain in the diagnostic;
- required `emojineer.toml` files at dependency roots.

Package content identity uses SHA-256 over a framed representation of the package's canonical manifest plus its owned `.emoji` source files. Dependency-owned source trees are excluded, including transitive dependency roots nested elsewhere beneath an ancestor package directory. This keeps one package's content hash from silently absorbing another package's source.

## Project validation

```text
emji check
emji check path/to/project
```

Validation includes:

1. strict manifest parsing;
2. root entry-file existence and compilation;
3. recursive local dependency resolution;
4. dependency entry-file existence and source-graph compilation;
5. deterministic lockfile freshness when a lockfile is present.

Train 8's source-module linker still defines the module root for each package. Cross-package import coordinates are a separate language-linker surface and are intentionally not smuggled into Train 10's package metadata implementation.

## Deterministic lockfile v2

Write or refresh the lockfile with:

```text
emji lock
emji lock path/to/project
```

v0.10 writes lock format 2:

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

The dependency records are sorted by package name and include transitive packages. Paths are rendered relative to the root project, so absolute checkout locations are not locked into the file. The lock contains no timestamps.

`manifest_hash` remains FNV-1a-64 over the canonical root manifest and is an identity/drift marker, not a security primitive. Dependency package content is pinned with SHA-256.

`emji check` recomputes the canonical lock text. Changes to dependency source, versions, manifests, graph topology, or paths make the lock stale until `emji lock` refreshes it.

## Inspect metadata

```text
emji show
emji show path/to/project
```

This displays the root package name, version, entry path, manifest hash, and declared direct dependencies.

## Relationship to modules

The project/package layer and source-module layer are distinct:

- `emojineer.toml` defines package metadata and local dependency edges;
- `🧩` identifies an Emojineer source module;
- `🔗 📜relative/path.emoji📜` imports a source module inside the current package root;
- `🔗 📜std:math📜` imports a built-in standard module;
- `📤` declares public module symbols.

Local dependency packages are now resolved and locked as real packages. Explicit source syntax for importing modules from those packages is the next linker train, rather than an implicit path escape that would weaken Train 8's project-root boundary.

See [MODULES.md](MODULES.md).
