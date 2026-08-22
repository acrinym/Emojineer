# `emji` Projects — Current Local Project Model

`emji` is Emojineer's project/package workflow. The current implementation provides deterministic local project metadata and module-graph validation. It deliberately does not pretend a remote registry exists yet.

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

The starter entry source is immediately usable by the normal `emojineer` CLI.

## Manifest

`emojineer.toml` is intentionally strict:

```toml
[package]
name = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"
```

Current required package fields:

- `name` — non-empty ASCII letters/digits plus `-` and `_`;
- `version` — semantic-version-shaped text such as `0.1.0`;
- `entry` — relative portable path to a `.emoji` source file.

The entry path:

- cannot be absolute;
- cannot contain `..` components;
- must use portable forward-slash syntax rather than backslashes;
- must end in `.emoji`.

Unknown keys and unsupported manifest sections are rejected rather than silently ignored.

## Project validation

```text
emji check
emji check path/to/project
```

Current validation includes:

1. manifest parsing and strict-field validation;
2. entry-file existence and regular-file checks;
3. compilation/validation of the entry's local Emojineer module graph;
4. lockfile readability and manifest-hash freshness when a lockfile is present.

Because Train 8 module compilation discovers the nearest enclosing `emojineer.toml`, that project directory also establishes the module root for the normal file-oriented `emojineer` commands.

For example, `src/main.emoji` may explicitly import another project-owned file such as `../lib/math.emoji` while canonical module resolution still prevents source imports from escaping the project root.

## Deterministic lock metadata

Write or refresh the lockfile with:

```text
emji lock
emji lock path/to/project
```

Current lock format:

```text
lock_version = 1
manifest_hash = "..."
package = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"
```

The lockfile contains no timestamp, so identical project metadata produces identical lock text.

`manifest_hash` is FNV-1a-64 over the canonical manifest representation. It is an identity/drift marker, **not** a cryptographic artifact-integrity checksum.

The canonical manifest representation orders and renders the current package fields deterministically.

## Inspect metadata

```text
emji show
emji show path/to/project
```

This displays:

- package name;
- version;
- entry path;
- canonical manifest hash.

## Relationship to modules

The project manifest and the source module system solve different layers:

- `emojineer.toml` defines the package/project root and entry source;
- `🧩` identifies an Emojineer source module;
- `🔗` currently imports project-local `.emoji` source paths;
- `📤` declares a module's public symbols;
- `emji check` validates the resulting entry module graph.

Module identities emitted into linked bytecode are deterministic root-relative paths and therefore do not depend on the absolute checkout directory.

See [MODULES.md](MODULES.md).

## Current boundary before dependency resolution

The current manifest has only `[package]`. It does **not yet** accept dependency declarations, and the CLI does not yet provide `emji add` or `emji remove`.

That is intentional. The next dependency train is responsible for introducing a real local/path dependency model, deterministic dependency graph locking, package content hashes, and the associated CLI. A remote registry and `emji publish` come only after a real registry protocol exists.
