# Emojineer Modules and Imports

Emojineer has a deterministic multi-source program model without making the language a wrapper around C++, Python, JavaScript, or another host language.

Modules are resolved and linked into Emojineer's AST before the existing compiler emits sovereign `EMJBC` bytecode.

## Syntax

A multi-source unit starts with one module declaration:

```emoji
🧩 🧮
```

The declaration must be the first statement. The module name is an emoji identifier and must be unique across the loaded graph.

Import another project-local source unit with a text path:

```emoji
🔗 📜math.emoji📜
🔗 📜lib/text.emoji📜
```

Import a built-in standard module with a deterministic `std:` specifier:

```emoji
🔗 📜std:math📜
🔗 📜std:arrays📜
🔗 📜std:text📜
```

Imports must appear before executable or ordinary declaration statements.

Export a module-owned global or function with:

```emoji
📤 🌟
📤 🧠
```

Exports may appear after the declaration they expose. Forward export declarations are also valid because exports are resolved against the whole source unit.

## Visibility

Each module has its own top-level global and function namespace.

A symbol stays private unless the defining module marks it with `📤`. Direct imports make the imported module's exported names available unqualified in the importer.

Imports do not re-export symbols. If A imports B and C imports A, C does not automatically gain B's exports. C must import B directly if it wants B's public surface.

A module-local declaration that collides with an imported symbol of the same kind is rejected. Two direct imports that export the same variable name or the same function name are also rejected. Emojineer does not silently choose a winner.

Exported globals are live bindings. Assignment through an imported exported global updates that same linked global value.

Standard modules obey these exact visibility and collision rules; they are not privileged namespaces that bypass the language model.

## Project-local module identity

Runtime/link identity is not derived from an absolute checkout path.

Each project-local source unit receives a canonical identity from its normalized path relative to the module root, for example:

```text
lib/math.emoji
```

That identity is stable when the same project is checked out in a different absolute directory. Equivalent module graphs from different roots are required to serialize identically.

## Standard module identity

Built-in standard modules have deterministic virtual identities equal to their specifiers:

```text
std:math
std:arrays
std:text
```

They are not materialized into the user's project and do not depend on an installation path. Their source is owned by the Emojineer toolchain and passes through the normal lexer/parser/linker/compiler pipeline.

The source-facing `🧩` module name remains subject to the same uniqueness rule across the loaded graph.

See [STDLIB.md](STDLIB.md).

## Module root and local paths

Ordinary file imports are confined to the current project/module root.

Rules:

- file import paths must be relative;
- file import paths must use portable forward slashes;
- file imports must target `.emoji` files;
- missing/non-file targets are errors;
- canonical path resolution may not escape the module root, including through `..` or symlinks.

When the `emojineer` CLI compiles a source file, it searches upward for the nearest `emojineer.toml`. If found, that project directory becomes the module root. Otherwise the entry file's directory is the root.

This allows a normal project entry such as `src/main.emoji` to import `../lib/math.emoji` while still preventing imports from escaping the project.

A `std:` import is not a filesystem path, so it is resolved against the built-in standard-module catalog instead.

## Loading and initialization

The linker performs deterministic depth-first dependency resolution in source import order.

Dependencies initialize before their importers. A module is loaded and initialized once per program even in a diamond dependency graph.

The same once-only rule applies when several project files import the same standard module.

## Cycles

Cyclic imports are rejected before bytecode compilation. Diagnostics include the cycle chain.

Standard modules participate in the same cycle machinery. In v0.9 they may import only other `std:` modules, which prevents a built-in standard module from acquiring ambient access to project files.

## Bytecode compatibility

Modules and the v0.9 standard library do not require a new bytecode version or new VM opcodes.

The linker rewrites module-local names into deterministic internal identities before invoking the existing compiler. The resulting program still uses the existing global/function bytecode machinery, so the current `EMJBC` v3 writer remains unchanged and v1/v2/v3 reader compatibility is preserved.

Bytecode tooling may display internal names such as:

```text
@module/lib/math.emoji::🧠
@module/std:math::🧭
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

`emojineer stdlib` lists the built-in `std:` modules.

`emji check` compiles/validates the project entry's complete module graph, including standard modules.

`fmt`, `lint`, and `explain` continue to operate on the individual source file they are given.

The REPL remains a buffered single-source session. Module syntax requires file-based compilation because linking requires a concrete program graph. The REPL's existing shared input-stream contract for `📥` is unchanged.

## Dependency frontier

Project-local paths and built-in `std:` modules are distinct from package dependencies.

Train 10 will add manifest-authorized local packages with explicit `pkg:` coordinates. Normal relative imports will remain confined to their own package root, so package support will not weaken the existing local path boundary.

Remote registry coordinates, network resolution, implicit re-exports, wildcard namespaces, and host-language module wrappers remain outside the current model.
