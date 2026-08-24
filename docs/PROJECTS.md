# `emji` Projects and Local Dependencies

`emji` is Emojineer's project/package workflow. In v0.11 it manages deterministic local/path dependency graphs and connects them to the source linker without pretending a remote registry exists.

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

This workflow supports local/path packages only. There is no fake network registry, no `publish`, and no remote version solver.

## Recursive package graph

Dependency resolution is recursive. Each package resolves dependency paths relative to its own package root, so a package may itself have local dependencies.

Resolution enforces:

- deterministic package-name ordering;
- one resolved root per package name;
- dependency key/target package-name agreement;
- cycle detection with the package chain in the diagnostic;
- required `emojineer.toml` files at dependency roots.

Package content identity uses SHA-256 over a framed representation of the package's canonical manifest plus its owned `.emoji` source files. Dependency-owned source trees are excluded, including transitive dependency roots nested elsewhere beneath an ancestor package directory. This keeps one package's content hash from silently absorbing another package's source.

## Importing package source

A declared dependency can now be used by the source linker with an explicit package coordinate:

```emoji
🧩 🚀
🔗 📜pkg:mathkit/src/main.emoji📜
```

The form is:

```text
pkg:<dependency>/<module-path>.emoji
```

The dependency must be declared directly by the package containing the importing source module. A transitive dependency is not automatically visible.

For example, if `app` declares `b`, and `b` declares `c`, source in `app` may import `pkg:b/...` but cannot import `pkg:c/...` unless `app` also declares `c` itself.

Normal relative source imports remain confined to their owning package root. They may not cross into a dependency-owned subtree, even when that dependency physically lives below the project directory. Likewise, `pkg:b/...` may not tunnel into a separately resolved package nested beneath `b`; that nested package must be imported through its own declared coordinate.

Package-qualified module identities are deterministic, for example:

```text
pkg:mathkit/src/main.emoji
```

Absolute checkout paths are never encoded into linked bytecode identities.

See [MODULES.md](MODULES.md) for the complete source-linking rules.

## Project validation

```text
emji check
emji check path/to/project
```

Validation includes:

1. strict manifest parsing;
2. root entry-file existence and package-aware compilation;
3. recursive local dependency resolution;
4. dependency entry-file existence and package-aware source-graph compilation;
5. deterministic lockfile freshness when a lockfile is present.

The file compiler and `emji check` share the same package-aware module linker behavior. `pkg:` coordinates are therefore not a separate project-only validation path.

Package graph failures use package diagnostics such as `cyclic package dependency`; source-module cycles use `cyclic module import`. These are deliberately separate layers.

## Deterministic lockfile v2

Write or refresh the lockfile with:

```text
emji lock
emji lock path/to/project
```

v0.11 continues to write lock format 2:

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

The project/package layer and source-module layer remain distinct but are now connected explicitly:

- `emojineer.toml` defines package metadata and local dependency edges;
- `🧩` identifies an Emojineer source module;
- `🔗 📜relative/path.emoji📜` imports a source module inside the current package root;
- `🔗 📜pkg:mathkit/src/main.emoji📜` imports a source module from a declared direct dependency;
- `🔗 📜std:math📜` imports a built-in standard module;
- `📤` declares public module symbols.

The package graph authorizes which dependency roots exist. The source linker decides which specific modules are imported. Neither layer silently grants ambient access to every transitive package or every file beneath the checkout root.
