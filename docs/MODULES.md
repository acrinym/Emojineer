# Train 8: Modules and Imports

Train 8 gives Emojineer a deterministic multi-file program model without making the language a wrapper around C++, Python, JavaScript, or another host language.

Modules are resolved and linked into Emojineer's AST before the existing compiler emits sovereign `EMJBC` bytecode.

## Syntax

A multi-file source unit starts with one module declaration:

```emoji
🧩 🧮
```

The declaration must be the first statement. The module name is an emoji identifier and must be unique across the loaded graph.

Import another local source unit with a text path:

```emoji
🔗 📜math.emoji📜
🔗 📜lib/text.emoji📜
```

Imports must appear before executable or ordinary declaration statements.

Export a module-owned global or function with:

```emoji
📤 🌟
📤 🧠
```

Exports may appear after the declaration they expose. Forward export declarations are also valid because exports are resolved against the whole source unit.

## Example

`math.emoji`:

```emoji
🧩 🧮

🐍 🌟 🔢 🟰 1

🛠️ 🧠 🫴 🍎 🤲
    📦 🍎 ➕ 🌟
🏁

📤 🌟
📤 🧠
```

`main.emoji`:

```emoji
🧩 🚀
🔗 📜math.emoji📜

📝 🧠 🫴 41 🤲
```

Output:

```text
42
```

## Visibility

Each module has its own top-level global and function namespace.

A symbol stays private unless the defining module marks it with `📤`. Direct imports make the imported module's exported names available unqualified in the importer.

Imports do not re-export symbols. If module A imports B and module C imports A, C does not automatically gain B's exports. C must import B directly if it wants to use B's public surface.

A module-local declaration that collides with an imported symbol of the same kind is rejected. Two direct imports that export the same variable name or the same function name are also rejected. Emojineer does not silently choose a winner.

Exported globals are live bindings. Assignment through an imported exported global updates that same linked global value.

## Module identity

Runtime/link identity is not derived from an absolute checkout path.

Each source unit receives a canonical module identity from its normalized path relative to the module root. For example:

```text
lib/math.emoji
```

That identity is stable when the same project is checked out in a different absolute directory. Tests serialize equivalent module graphs from two different temporary roots and require byte-for-byte identical `EMJBC` output.

The `🧩` emoji module name is the source-facing name. It is separately required to be unique across the graph so diagnostics never have to guess which source unit a module name refers to.

## Module root and paths

Imports are intentionally local in Train 8.

Rules:

- import paths must be relative;
- import paths must use portable forward slashes;
- imports must target `.emoji` files;
- missing/non-file targets are errors;
- canonical path resolution may not escape the module root, including through `..` or symlinks.

When the `emojineer` CLI compiles a source file, it searches upward for the nearest `emojineer.toml`. If found, that project directory becomes the module root. Otherwise the entry file's directory is the root.

This allows a normal project entry such as `src/main.emoji` to import `../lib/math.emoji` while still preventing imports from escaping the project.

## Loading and initialization

The linker performs deterministic depth-first dependency resolution in source import order.

Dependencies initialize before their importers. A module is loaded and initialized once per program even in a diamond dependency graph.

For example, if left and right both import shared, shared is linked once.

## Cycles

Cyclic imports are rejected before bytecode compilation.

Diagnostics include the cycle chain, for example:

```text
cyclic module import: a.emoji -> b.emoji -> a.emoji
```

There is no partially initialized cyclic-module state in Train 8.

## Bytecode compatibility

Train 8 does not require a new bytecode version or new VM opcodes.

The linker rewrites module-local names into deterministic internal identities before invoking the existing compiler. The resulting program still uses the existing global/function bytecode machinery, so the current `EMJBC` v3 writer remains unchanged and the v1/v2/v3 reader compatibility is preserved.

Bytecode tooling may display internal names such as:

```text
@module/lib/math.emoji::🧠
```

Those names are linker metadata, not source syntax.

## CLI behavior

The file-based commands are module aware:

```text
emojineer check main.emoji
emojineer run main.emoji
emojineer dump main.emoji
emojineer compile main.emoji
```

`emji check` also compiles/validates the project entry's local module graph after manifest and entry checks.

`fmt`, `lint`, and `explain` continue to operate on the individual source file they are given.

The REPL remains a buffered single-source session. Module syntax requires file-based compilation because `🔗` needs a concrete module root and relative source location. The REPL's existing shared input-stream contract for `📥` is unchanged.

## Deliberate Train 8 boundaries

Train 8 does not invent a remote registry, remote package coordinates, network resolution, implicit re-exports, wildcard namespaces, or host-language module wrappers.

Those concerns belong to later package/dependency and interop trains after the local language model is stable.
