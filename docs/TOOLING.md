# REPL and Bytecode Inspection

This page focuses on Emojineer's interactive and bytecode-inspection tools. For the complete command set, see [CLI.md](CLI.md).

## Buffered REPL

Start an interactive session:

```text
emojineer repl
emojineer repl --cer cer/example.json
```

The v0.8 REPL buffers source lines so multiline functions, `🤔` blocks, and `🔁` loops can be entered naturally.

Commands:

- `:run` — compile and execute the current session;
- `:show` — print the current session source;
- `:explain` — explain the current session tokens;
- `:clear` — clear the session;
- `:help` — show commands;
- `:quit` — exit.

### Input-stream contract

The REPL and the VM intentionally share the same input stream.

For example:

```text
🐍 👤 🔤 🟰 📥
📝 👤
:run
Ada
:quit
```

After `:run`, `📥` consumes `Ada`. This is deliberate interactive behavior, not an input-stream bug.

### Module boundary

The REPL is currently a single-source buffered session. `🧩`, `🔗`, and `📤` require file-based compilation because module imports depend on concrete source paths and a module root.

## Compile and inspect source bytecode

```text
emojineer dump examples/collections.emoji
emojineer dump examples/modules/main.emoji
```

`dump` compiles source in memory, including module linking when applicable, and prints the generated chunk.

## Inspect serialized bytecode

```text
emojineer disasm examples/collections.emjbc
```

`disasm` reads an existing `.emjbc`, verifies it, and then prints:

- constant-pool entries;
- function metadata;
- instruction indices;
- opcode names;
- operands;
- source-line metadata.

Module-linked programs may expose deterministic internal symbol names such as:

```text
@module/lib/math.emoji::🧠
```

Those names are linker metadata, not source syntax.

## Bytecode compatibility

The current writer emits EMJBC v3. The reader accepts v1, v2, and v3. Module support did not require a new bytecode version because module resolution happens before bytecode generation.

See [BYTECODE.md](BYTECODE.md) for the serialized format, verifier rules, opcode set, and VM execution model.
