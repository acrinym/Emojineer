# Emojineer Standard Library — v0.9 Foundation

Train 9 establishes the first standard-library modules owned by Emojineer.

The important architectural rule is that these are **not host-language wrappers**. Each standard module is stored as Emojineer source and is lexed, parsed, linked, compiled to EMJBC, and executed by the same VM as user code.

Import a standard module with a `std:` specifier:

```emoji
🧩 🚀
🔗 📜std:math📜

📝 🧭 🫴 ➖ 9 🤲
```

Standard modules have deterministic virtual identities such as `std:math`; they do not depend on the absolute location of the compiler or project checkout.

List the modules built into the toolchain:

```text
emojineer stdlib
```

## `std:math`

```emoji
🔗 📜std:math📜
```

Exports:

| Function | Parameters | Behavior |
| --- | --- | --- |
| `🧭` | value | absolute value |
| `🤏` | left right | smaller value |
| `👐` | left right | larger value |
| `🎚️` | value low high | clamp value to the inclusive low/high range |

Example:

```emoji
📝 🧭 🫴 ➖ 8 🤲
📝 🤏 🫴 7 2 🤲
📝 👐 🫴 7 2 🤲
📝 🎚️ 🫴 19 0 10 🤲
```

Output:

```text
8
2
7
10
```

These helpers use ordinary Emojineer comparison, negation, condition, and return semantics.

## `std:arrays`

```emoji
🔗 📜std:arrays📜
```

Exports:

| Function | Parameters | Behavior |
| --- | --- | --- |
| `🧲` | array value | structural membership test; returns `✅` or `❌` |
| `🧮` | array | numeric sum, beginning at zero |
| `🔃` | array | returns a reversed array using value-style append |

Example:

```emoji
🐍 🧺 📚 🟰 📚 🫴 3 1 2 🤲
📝 🧲 🫴 🧺 1 🤲
📝 🧮 🫴 🧺 🤲
📝 🔃 🫴 🧺 🤲
```

Output:

```text
✅
6
[2, 1, 3]
```

`🧮` relies on ordinary `➕`, so a nonnumeric member produces the same runtime type error as a direct invalid addition.

## `std:text`

```emoji
🔗 📜std:text📜
```

Exports:

| Function | Parameters | Behavior |
| --- | --- | --- |
| `🈳` | text | true when grapheme length is zero |
| `🪢` | left right | concatenate two text values |
| `🔂` | text count | repeat text `count` times for a nonnegative whole-number count; fractional counts return empty text |

Example:

```emoji
📝 🈳 🫴 📜📜 🤲
📝 🪢 🫴 📜hello 📜 📜world📜 🤲
📝 🔂 🫴 📜ha📜 3 🤲
```

Output:

```text
✅
hello world
hahaha
```

`🔂` currently uses the core remainder operation to accept whole-number counts. Zero and negative whole-number counts naturally produce empty text; fractional counts are rejected to empty text until the language gains richer error values/contracts.

## Visibility and collisions

Standard modules obey the same import/export rules as file modules:

- only explicit `📤` symbols become visible to the importer;
- imports do not implicitly re-export;
- a local declaration colliding with an imported standard-library export is rejected;
- importing two modules that expose the same emoji name is rejected rather than silently choosing one.

## Bytecode compatibility

Train 9 does not add an opcode or change the serialized format. Standard-library source is linked before the existing compiler emits normal EMJBC v3 bytecode. The v1/v2/v3 reader compatibility remains unchanged.

## Security boundary

This foundation intentionally provides no ambient filesystem, network, process, clock, randomness, shell, or FFI access. Those capabilities require an explicit capability model in a later train rather than being smuggled into the standard library as host-language escape hatches.

## Future standard-library growth

Later library trains can add richer text transforms, math primitives that need VM/native intrinsics, data formats, time, files, networking, and other facilities. When an operation cannot be expressed in Emojineer source, it should enter through a documented Emojineer-owned intrinsic/capability boundary rather than by exposing arbitrary C++/Python/JavaScript calls.
