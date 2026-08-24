# Emojineer Modules and Imports

Emojineer has a deterministic multi-source program model without making the language a wrapper around C++, Python, JavaScript, or another host language.

Modules are resolved and linked into Emojineer's AST before the existing compiler emits sovereign `EMJBC` bytecode.

## Syntax

A multi-source unit starts with one module declaration:

```emoji
🧩 🧮
```

The declaration must be the first statement. The module name is an emoji identifier and must be unique across the loaded graph.

Import another source unit inside the current package with a relative text path:

```emoji
🔗 📜math.emoji📜
🔗 📜lib/text.emoji📜
```

Import a module from a declared direct package dependency with a package-qualified specifier:

```emoji
🔗 📜pkg:mathkit/src/main.emoji📜
🔗 📜pkg:textkit/lib/text.emoji📜
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

Package modules and standard modules obey these exact visibility and collision rules. Neither package coordinates nor `std:` are privileged namespaces that bypass ordinary `📤` export semantics.

## Module identity

Runtime/link identity is never derived from an absolute checkout path.

A root-package source unit keeps its normalized package-relative identity, for example:

```text
lib/math.emoji
```

A dependency-package module receives a deterministic package-qualified identity:

```text
pkg:mathkit/src/main.emoji
pkg:textkit/lib/text.emoji
```

Built-in standard modules use their virtual specifiers directly:

```text
std:math
std:arrays
std:text
```

These identities remain stable when the same package graph is checked out in a different absolute directory. Equivalent source/package graphs from different checkout roots are required to serialize identically.

The source-facing `🧩` module name remains subject to the same uniqueness rule across the entire loaded graph, including project-local modules, dependency-package modules, and standard modules.

## Package roots and local paths

Ordinary file imports are confined to the source module's owning package root.

Rules:

- file import paths must be relative;
- file import paths must use portable forward slashes;
- file imports must target `.emoji` files;
- missing/non-file targets are errors;
- canonical path resolution may not escape the current package root through `..` or symlinks;
- a relative import may not enter a different resolved package root nested under the current package.

When the `emojineer` CLI compiles a source file, it searches upward for the nearest `emojineer.toml`. If found, that package directory becomes the module root and its real `PackageGraph` is supplied to the linker whenever module syntax is present. Otherwise the entry file's directory is the ordinary non-package module root.

A normal project entry such as `src/main.emoji` can still import another root-package file such as `../lib/math.emoji`, provided that canonical target remains owned by the root package.

A relative filesystem path is never the mechanism for importing dependency-owned source.

## `pkg:` package coordinates

The package import form is:

```text
pkg:<dependency>/<module-path>.emoji
```

The dependency name must be present in the importing package's own declared `[dependencies]` set. The linker resolves that name through the already-resolved `PackageGraph`, then resolves the module path relative to that dependency package's canonical root.

Package coordinates enforce several boundaries:

- only declared **direct** dependencies are ambient to a package's source;
- transitive dependencies do not become automatically importable;
- `pkg:` module paths must remain inside the named dependency's root;
- a `pkg:` path may not tunnel through one package into another package physically nested beneath it;
- if a nested package is separately declared, it must be imported through its own `pkg:<name>/...` coordinate;
- package/module resolution uses canonical filesystem paths, so symlink escapes are rejected.

This means a root package depending on `b`, where `b` depends on `c`, may import `pkg:b/...` but may not import `pkg:c/...` unless the root package itself explicitly declares `c`.

## Standard module identity

A `std:` import is not a filesystem or package path. It is resolved against the built-in standard-module catalog.

Standard modules are not materialized into the user's project and do not depend on an installation path. Their source is owned by the Emojineer toolchain and passes through the normal lexer/parser/linker/compiler pipeline.

See [STDLIB.md](STDLIB.md).

## Loading and initialization

The linker performs deterministic depth-first source-module resolution in source import order after the package graph has been resolved.

Dependencies initialize before their importers. A module is loaded and initialized once per program even in a diamond source dependency graph.

The same once-only rule applies when several project or package files import the same standard module or the same package-qualified module identity.

## Cycles

Package dependency cycles and source-module import cycles are distinct errors.

Package cycles are rejected by the package resolver with a `cyclic package dependency` chain before package-aware linking begins.

Source-module cycles are rejected by the module linker with a `cyclic module import` chain. Package-owned source modules appear in that chain with deterministic `pkg:<package>/...` identities.

Standard modules participate in the source-module cycle machinery. They may import only other `std:` modules, preventing a built-in standard module from acquiring ambient access to project or package files.

## Bytecode compatibility

Cross-package modules do not require a new bytecode version or new VM opcodes.

The linker rewrites module-local names into deterministic internal identities before invoking the existing compiler. The resulting program still uses the existing global/function bytecode machinery, so the current `EMJBC` v3 writer remains unchanged and v1/v2/v3 reader compatibility is preserved.

Bytecode tooling may display internal names such as:

```text
@module/lib/math.emoji::🧠
@module/pkg:mathkit/src/main.emoji::🧠
@module/std:math::🧭
```

Those names are linker metadata, not source syntax.

## CLI behavior

The file-based commands are package/module aware:

```text
emojineer check src/main.emoji
emojineer run src/main.emoji
emojineer dump src/main.emoji
emojineer compile src/main.emoji
```

When the source belongs to an `emojineer.toml` package and uses module syntax, these commands resolve the same package graph and package-qualified imports used by `emji check`.

`emojineer stdlib` lists the built-in `std:` modules.

`emji check` compiles/validates the root package entry and each dependency package entry using the same package-aware file linker path.

`fmt`, `lint`, and `explain` continue to operate on the individual source file they are given.

The REPL remains a buffered single-source session. Module syntax requires file-based compilation because linking requires a concrete source/package graph. The REPL's existing shared input-stream contract for `📥` is unchanged.

## Boundaries

v0.11 supports project-local imports, declared local/path package imports, and built-in standard modules as three explicit source namespaces.

It does not implement a remote registry, network resolution, implicit re-exports, wildcard namespaces, host-language module wrappers, or arbitrary filesystem imports outside package ownership boundaries.
