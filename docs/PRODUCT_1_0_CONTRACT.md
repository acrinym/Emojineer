# Emojineer 1.0 Product Contract

Emojineer 1.0 is the first release intended for people who did not design the
language. The release succeeds when a new user can install Emojineer, discover
the emoji vocabulary, create a project, run and debug useful programs, share
them, and trust that 1.x upgrades preserve documented behavior.

## Install-to-first-program target

A supported user journey must require no C++ toolchain:

1. install or unpack a published Emojineer distribution;
2. verify `emojineer --version` and `emji --version`;
3. create a project with `emji init`;
4. open it in a supported editor and use semantic emoji insertion/completion;
5. run, format, check, and debug the program;
6. add/use a package without weakening runtime authority.

The no-install browser playground is a second supported entry path. It must run
the real Emojineer semantics, not a JavaScript reimplementation.

## Supported product surfaces

The 1.0 release track targets:

- Windows, Linux, and macOS distributable toolchains;
- VS Code/Cursor-compatible editor packaging around `emojineer-lsp`;
- practical CLI/data applications with structured data and explicit failures;
- capability-gated host I/O suitable for ordinary utilities;
- first-class bytes and explicit encoding/decoding utilities;
- a progressive tour, project templates, and representative examples;
- a sandboxed browser playground with shareable source;
- Emojineer-native web markup lowering through a typed document/DOM IR.

The sovereign compiler, VM, package model, capability system, interop ABI, and
EASM remain the semantic authority for these surfaces.

## Compatibility promise

Before 1.0 release, the project freezes and documents the 1.x compatibility
rules for source grammar, EMJBC readers, package/artifact formats, manifest and
lock behavior, standard modules, capability names, EMJABI contracts, and EASM.

Within 1.x, documented source programs and supported serialized artifacts must
not silently change meaning. Intentional incompatibilities require explicit
versioning, migration guidance, and tests for the previous supported form.

## Explicit non-goals for 1.0

- Emoji Stories is not part of 1.0.
- Breakable or playful physical-note secret-code schemes are not part of 1.0.
- Retro machine/profile targets are not a 1.0 deliverable unless required by
  portability work; they remain a future domain for the runtime/EASM stack.
- Browser support must not create a second JavaScript Emojineer interpreter.
- Web markup must not be HTML hidden behind front matter or string templates.
- Productization must not weaken default-deny runtime authority or package
  provenance guarantees.

## Reserved post-1.0 / 2.0 direction

**Emojineer 2.0: Emoji Stories** is reserved as a separate design space for
human-passable emoji narratives/encodings, including deliberately breakable,
fun physical-note codes. Future work may explore canonicalization, checksums,
copy tolerance, redundancy, and puzzle-oriented codecs, but must not be
presented as cryptography unless it actually provides cryptographic security.

Semantic-compression/metaprogramming research, native backends, and serious
retro targets remain future product trains after the 1.0 usability arc unless
a 1.0 car proves a prerequisite is required.
