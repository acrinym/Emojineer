# Emojineer CLI and Toolchain

Emojineer builds two command-line executables:

- `emojineer` — source, bytecode, formatting, REPL, and execution tools;
- `emji` — local project and lockfile workflow.

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

Compiles the source and executes it on the Emojineer VM. If the entry contains module syntax, the local module graph is resolved before compilation.

### Check

```text
emojineer check program.emoji
```

Parses, resolves modules when present, compiles, and verifies the generated chunk without executing it.

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

Compiles source in memory and disassembles the resulting chunk.

### Compile

```text
emojineer compile program.emoji
emojineer compile program.emoji -o output.emjbc
```

Writes serialized sovereign `EMJBC` bytecode. Without `-o`, the source extension is replaced with `.emjbc`.

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

- `:run` — compile and execute the current buffer;
- `:show` — print the current source buffer;
- `:explain` — explain current tokens;
- `:clear` — clear the buffer;
- `:help` — show commands;
- `:quit` — leave the REPL.

The REPL and VM intentionally share the same input stream. After `:run`, `📥` consumes the following input line from that stream.

The REPL is currently single-source. `🧩`, `🔗`, and `📤` require file-based compilation because imports need a concrete source path and module root.

## Custom Emoji Registry options

Source-oriented commands accept one or more CER packs:

```text
emojineer run program.emoji --cer cer/a.json --cer cer/b.json
```

CER packs extend lexical spellings by mapping registered emoji sequences into existing core token kinds. They do not execute host-language code or define a second runtime.

`exec` and `disasm` operate on already-compiled bytecode and therefore do not accept source CER options.

## `emji` project commands

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

Validates the manifest, entry source, current lock metadata when present, and the entry's local Emojineer module graph.

### Write lockfile

```text
emji lock
emji lock path/to/project
```

Writes deterministic lock metadata with no timestamp.

### Show project metadata

```text
emji show
emji show path/to/project
```

Displays package name, version, entry path, and canonical manifest hash.

## Exit behavior

Successful commands return zero. Parse, compile, bytecode, project, I/O, or runtime failures report an `emojineer:` or `emji:` diagnostic and return nonzero. `lint` returns nonzero when style diagnostics are present.

## Current boundaries

The current toolchain has no remote package registry, package publication command, language server, debugger, host FFI, network API, or filesystem standard-library API yet. Those are later product trains rather than undocumented hidden behavior.
