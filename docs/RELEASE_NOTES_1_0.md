# Emojineer 1.0 Release Notes

Status: release-candidate notes. The repository remains version 0.23.0 until the final 1.0 release decision is complete.

## What 1.0 adds

- installable Windows, Linux, and macOS toolchain archives with checksums;
- a packaged VS Code/Cursor editor experience backed by the native LSP;
- practical records/results, bytes, codecs, explicit program arguments, bounded host I/O, and verifier-visible intrinsics;
- four `emji init` templates and a five-minute executable learning path;
- a static WebAssembly playground that runs the real C++ compiler and production VM in hard sandbox mode;
- native `.emjweb` authoring with a typed document IR, semantic accessible HTML lowering, safe styling, and explicit sandboxed behavior bindings;
- typed host/WASM interop and the verified EASM low-level surface from the preceding trains;
- deterministic package artifacts, registry discovery/transport, immutable publication, and lock-format-3 registry provenance.

## Compatibility baseline

Emojineer 1.0 freezes the public 1.x rules in [COMPATIBILITY_1_X.md](COMPATIBILITY_1_X.md). The EMJBC writer emits v10 and the reader supports v1 through v10. `EMJPKG1`, lock format 3, capability names, `EMJABI1`, `EASM1`, and the documented registry protocols are compatibility-sensitive.

## Security and authority

Default execution remains zero-authority. Native host access requires explicit capability grants, and browser execution uses `ExecutionMode::Sandbox`. Bytecode capability metadata is verifier-bound to actual host/interop calls. Browser web behavior also executes through the same production VM rather than page-side JavaScript semantics.

Release hardening adds deterministic hostile-input corpora for the lexer/parser, EMJBC, EASM1, EMJPKG1, and registry readers, plus legacy EMJBC v1-v9 framing regression coverage.

## Upgrade notes

Pre-format-3 project locks should be regenerated. Users should review explicit capability requirements before granting host authority. See [UPGRADING_TO_1_0.md](UPGRADING_TO_1_0.md) for the migration checklist.

## Release blocker

The project owner must make and record the release-license decision before version 1.0.0 can be published. Release automation deliberately blocks 1.x packaging when the root license file and editor-package license metadata are absent.
