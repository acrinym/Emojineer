# Emojineer CLI and Toolchain

Emojineer builds two command-line executables:

- `emojineer` - source, bytecode, formatting, REPL, standard-library, package-aware linking, and execution tools;
- `emji` - project, local dependency, and lockfile workflow.

The current toolchain version is **0.11**.

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

## Source commands

### Run

```text
emojineer run program.emoji
```

Compiles the source and executes it on the Emojineer VM. If the entry contains module syntax, the module graph is resolved before compilation. When an enclosing `emojineer.toml` exists, the real package graph is also resolved so imports may use project-local `.emoji` paths, declared `pkg:<dependency>/<module>.emoji` coordinates, or built-in `std:<module>` specifiers.

### Check

```text
emojineer check program.emoji
```

Parses, resolves package/module context when present, compiles, and verifies the generated chunk without executing it.

### Explain

```text
emojineer explain program.emoji
```

Prints meaningful lexer tokens with semantic descriptions. Custom CER metadata, aliases, and semantic IDs are shown when applicable.

`explain` is file-local. It does not traverse imports.

### Format

```text
emojineer fmt program.emoji
emojineer fmt program.emoji -o formatted.emoji
```

Produces canonical indentation and LF source form while preserving comments and whitespace that belongs to multiline string literals.

### Lint

```text
emojineer lint program.emoji
```

Diagnoses differences from canonical formatting, including noncanonical CR/CRLF source line endings and missing final newline.

### Dump source bytecode

```text
emojineer dump program.emoji
```

Compiles source in memory, including local, package, and standard-module linking, and disassembles the resulting chunk.

### Compile

```text
emojineer compile program.emoji
emojineer compile program.emoji -o output.emjbc
```

Writes serialized sovereign `EMJBC` bytecode. Without `-o`, the source extension is replaced with `.emjbc`.

Package-qualified module identity is deterministic and checkout-portable. Dependency modules are represented internally with identities such as `pkg:mathkit/src/main.emoji`, never absolute checkout paths.

## Package-aware imports

Given:

```toml
[dependencies]
mathkit = "../mathkit"
```

source in the declaring package can import:

```emoji
🔗 📜pkg:mathkit/src/main.emoji📜
```

Only declared direct dependencies are visible through `pkg:`. Transitive packages do not become ambient imports. Relative source imports stay within the current package's owned source tree and cannot tunnel into a resolved dependency subtree. A `pkg:` coordinate likewise cannot tunnel through one dependency into a separately resolved nested package.

## Standard library catalog

```text
emojineer stdlib
```

Prints the standard modules built into this Emojineer toolchain and a short description of each one. The current catalog contains:

- `std:math`;
- `std:arrays`;
- `std:text`.

Standard modules are implemented as Emojineer source and compiled through the normal language pipeline. See [STDLIB.md](STDLIB.md).

`stdlib` takes no source input and does not accept `--cer` or `-o`.

## Bytecode commands

### Execute

```text
emojineer exec program.emjbc
```

Reads and verifies bytecode, then executes it on the VM.

### Disassemble

```text
emojineer disasm program.emjbc
```

Reads and verifies bytecode, then prints constants, function metadata, and instructions.

## REPL

```text
emojineer repl
emojineer repl --cer cer/example.json
```

The REPL buffers source until `:run`.

Commands:

- `:run` - compile and execute the current buffer;
- `:show` - print the current source buffer;
- `:explain` - explain current tokens;
- `:clear` - clear the buffer;
- `:help` - show commands;
- `:quit` - leave the REPL.

The REPL and VM intentionally share the same input stream. After `:run`, `📥` consumes the following input line from that stream.

The REPL is currently single-source. `🧩`, `🔗`, and `📤`, including `pkg:` and `std:` imports, require file-based compilation because imports need a concrete source/package graph.

## Custom Emoji Registry options

Source-oriented commands accept one or more CER packs:

```text
emojineer run program.emoji --cer cer/a.json --cer cer/b.json
```

CER packs extend lexical spellings by mapping registered emoji sequences into existing core token kinds. They do not execute host-language code or define a second runtime.

`exec` and `disasm` operate on already-compiled bytecode and therefore do not accept source CER options.

## `emji` project and dependency commands

### Initialize

```text
emji init my-project
emji init my-project --name signal_lab
```

Creates a strict `emojineer.toml`, deterministic `emojineer.lock`, and `src/main.emoji` starter source.

### Check project

```text
emji check
emji check path/to/project
```

Validates the root manifest and entry source, recursively resolves declared local/path package dependencies, validates root and dependency entry source graphs with package-aware linking, and compares an existing lockfile with the canonical dependency graph. Local, `pkg:`, and `std:` imports therefore pass through the same file linker used by ordinary source commands.

### Write lockfile

```text
emji lock
emji lock path/to/project
```

Writes deterministic lockfile v2 metadata with no timestamp. Dependency records include package version, root-relative resolved path, direct dependency names, and a SHA-256 content identity computed from the package's canonical manifest plus package-owned `.emoji` sources.

### Show project metadata

```text
emji show
emji show path/to/project
```

Displays package name, version, entry path, canonical manifest hash, and direct dependency declarations.

### Add a local dependency

```text
emji add mathkit ../mathkit
emji add mathkit ../mathkit path/to/project
```

The package name must match the target dependency's manifest name. The dependency path must be relative. The candidate recursive graph is validated before the manifest is written, then the manifest and lockfile are canonicalized.

### Remove a local dependency

```text
emji remove mathkit
emji remove mathkit path/to/project
```

Removes the named direct dependency, canonicalizes the manifest, and refreshes the lockfile.

## Exit behavior

Successful commands return zero. Parse, compile, bytecode, project, dependency, I/O, or runtime failures report an `emojineer:` or `emji:` diagnostic and return nonzero. `lint` returns nonzero when style diagnostics are present.

## Current boundaries

The v0.11 toolchain has real local/path package dependency resolution and explicit cross-package module imports, but no remote registry, package publication command, remote version solver, or download cache. Package access remains explicit and direct-dependency scoped. The standard library remains pure and capability-free, and the toolchain has no arbitrary host FFI, network standard-library API, or filesystem standard-library API.
