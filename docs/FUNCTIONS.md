# Functions, Calls, and Scope

Train 2 adds user-defined functions to the sovereign Emojineer VM.

## Syntax

```emoji
🛠️ 🥐 🫴 🍎 🤲
  🤔 🍎 🔽 2
    📦 1
  🏁
  📦 🍎 ✖️ 🥐 🫴 🍎 ➖ 1 🤲
🏁

📝 🥐 🫴 6 🤲
```

- `🛠️` begins a function declaration.
- The next emoji identifier is the function name.
- `🫴 ... 🤲` contains zero or more emoji parameters.
- `📦 expression` returns one value.
- `🏁` closes the function body.
- A function call uses the function emoji followed by `🫴 ... 🤲` arguments.

## Runtime semantics

Each call creates a real VM call frame. Parameters and function-local `🐍` declarations live in per-call local slots, so recursion is isolated correctly. Globals remain readable and assignable when a name does not resolve to a local slot.

Calls are arity-checked during compilation and again protected by VM metadata. Call depth is bounded to prevent unbounded native memory growth. A function that reaches the end without `📦` returns `❌`.

Nested function declarations are intentionally not part of Train 2. They are reserved for a later lexical-closure train.

## Bytecode

`EMJBC` v2 adds a function table plus `LoadLocal`, `StoreLocal`, `Call`, and `Return` instructions. The reader retains support for v1 bytecode and maps the original opcode numbering explicitly, so old artifacts are not reinterpreted under the expanded v2 enum.

The normal bytecode verifier validates function metadata, jump targets, call indices, and operand references before execution.
