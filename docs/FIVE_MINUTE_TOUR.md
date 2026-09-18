# Emojineer in Five Minutes

This tour starts from an installed Emojineer toolchain. If the commands are not
on PATH yet, begin with [INSTALL.md](INSTALL.md).

## 1. Prove the toolchain

```bash
emojineer --version
emji --version
emojineer-lsp --version
```

Create the default project:

```bash
emji init hello
emojineer run hello/src/main.emoji
```

The generated program prints `Hello from Emojineer 🚀`.

## 2. Start from a useful template

`emji init` offers four first-party templates:

```bash
emji init tool --template cli
emji init records --template data
emji init fetcher --template network
emji init hello --template hello
```

- `hello` is the smallest runnable project;
- `cli` reads explicit arguments supplied after `--`;
- `data` demonstrates named records and keyed updates;
- `network` demonstrates a capability-gated HTTPS request.

The default remains `hello`.

## 3. Run a real CLI program

```bash
emojineer run examples/cli_args.emoji -- alpha "two words"
```

The source uses `🧳` to read the exact explicit argument array. Reading argv
does not require host authority.

Host resources are different. Inspect before granting:

```bash
emojineer capabilities examples/network_request.emoji
emojineer run examples/network_request.emoji
```

The second command is denied before instruction zero because no `network`
grant was supplied.

## 4. Use structured data and encoding

Run:

```bash
emojineer run examples/practical_data.emoji
emojineer run examples/encoding.emoji
emojineer run examples/calculator.emoji
```

The examples cover records/results, explicit bytes and UTF-8/hex/Base64 codecs,
and ordinary arithmetic.

## 5. Use another package

The checked-in package example has two real packages:

```bash
emji check examples/package_use/app
emojineer run examples/package_use/app/src/main.emoji
```

The app declares `mathkit` directly and imports
`pkg:mathkit/src/main.emoji`. There is no ambient transitive package access.

## 6. Explore the deeper surfaces

The example corpus also includes:

- `examples/cer.emoji` with `cer/example.json`;
- `examples/interop.emoji` for the Train 21 typed adapter contract;
- `examples/lowlevel.easm` for Train 22 EASM;
- `examples/modules/` for source modules;
- collection, function, stdlib, input, and countdown examples.

Useful commands:

```bash
emojineer run examples/cer.emoji --cer cer/example.json
emojineer interop examples/interop.emoji
emojineer easm-run examples/lowlevel.easm
```

From here, continue with [LANGUAGE.md](LANGUAGE.md), [PROJECTS.md](PROJECTS.md),
and [CAPABILITIES.md](CAPABILITIES.md).
