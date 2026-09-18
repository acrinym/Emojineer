# Emojineer 1.0 Release Readiness

This checklist records the Car 10 release gate. Technical qualification can be completed before the final owner-controlled license decision, but 1.0 must not be tagged or published until every blocking item is green.

## Compatibility and hostile input

- [x] 1.x compatibility policy documented.
- [x] EMJBC v1-v9 historical layouts exercised against the current reader; v10 is covered by current round-trip tests.
- [x] Frozen `EMJPKG1` framing exercised independently of the current writer.
- [x] Deterministic hostile-input corpora cover lexer/parser, EMJBC, EASM1, EMJPKG1, registry receipt, and registry endpoint readers.
- [x] Unsupported/truncated EMJBC fails explicitly.

## Product acceptance

- [x] Practical runtime, capabilities, interop, EASM, packages, registry, debugger, LSP, examples, browser runtime, and native web markup are in the unified CTest suite.
- [x] Actual Emscripten 6.0.5 browser bundle executes production semantics, Unicode 14/15 identifiers, native web rendering/behavior, and sandbox denial.
- [x] Five-minute templates/examples are executable acceptance tests.
- [x] Release notes and 1.0 upgrade guidance are present.

## Distribution qualification

- [x] Release workflow defines Windows, Linux, and macOS build/test/install/package jobs.
- [x] Installed-tree smoke exists for all three platforms, including the Windows portable layout.
- [x] Local clean Linux Release build passed 32/32 tests, installed to a fresh staging prefix, executed a generated data-template project, and produced a checksummed TGZ.
- [x] Local VS Code package validation passed and produced a VSIX; its only packaging warning is the intentionally unresolved missing LICENSE.
- [ ] Current PR release matrix has passed on GitHub for the final Car 10 commit.

## Release metadata and license

- [x] CMake and VS Code package versions are checked for equality by release automation.
- [x] 1.x release automation requires a tracked root license file and editor-package license metadata.
- [ ] **BLOCKING OWNER DECISION:** choose and record the Emojineer release license. Do not infer or invent this choice.
- [ ] After the license decision, set the 1.0.0 project/editor versions and verify the release tag exactly matches them.

## Merge/release control

PR #31 may be qualified and made merge-ready, but merge remains subject to Justin's merge whistle. A merge is not the same action as tagging/publishing 1.0.
