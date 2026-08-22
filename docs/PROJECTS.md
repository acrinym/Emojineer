# Train 7 — `emji` Project Workflow

Train 7 introduces the local project foundation for Emojineer's future package manager.

## Initialize a project

```text
emji init my-project
emji init my-project --name signal_lab
```

This creates:

```text
my-project/
├── emojineer.toml
├── emojineer.lock
└── src/
    └── main.emoji
```

The default entry prints a small hello message and can immediately be run with the normal `emojineer` CLI.

## Manifest

`emojineer.toml` is intentionally strict in this train:

```toml
[package]
name = "signal_lab"
version = "0.1.0"
entry = "src/main.emoji"
```

Required fields:

- `name`: ASCII letters/digits plus `-` and `_`;
- `version`: semantic-version-shaped string;
- `entry`: relative `.emoji` path that cannot escape the project root.

Unknown package keys and unsupported sections are rejected rather than silently ignored.

## Validate a project

```text
emji check
emji check path/to/project
```

Validation checks manifest structure, entry-file presence, and lock freshness.

## Lock metadata

```text
emji lock
```

`emojineer.lock` is deterministic and contains no timestamps. Train 7 records a stable FNV-1a-64 hash of the canonical manifest plus the resolved local package metadata. This is an identity/drift detector, not a cryptographic package-integrity checksum. A later registry train can add cryptographic artifact hashes when remote package distribution exists.

## Inspect project metadata

```text
emji show
```

Displays the package name, version, entry point, and canonical manifest hash.

This train intentionally does not fake `add`, `publish`, or remote dependency resolution before a real registry protocol exists.
