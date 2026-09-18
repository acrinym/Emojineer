# Upgrading to Emojineer 1.0

## Projects and locks

Regenerate pre-format-3 lockfiles with `emji lock` or `emji sync`. Lock format 3 is the 1.x contract and records path or registry provenance explicitly. Do not hand-edit old locks into the new shape.

Run `emji check <project>` after regeneration. For registry-backed projects, run an online `emji sync` once to establish verified cache/store state, then exercise `emji sync --offline` if offline reproducibility matters to the project.

## Bytecode

Existing supported EMJBC v1-v10 input remains readable. Recompilation is not required merely because the 1.0 writer emits v10. Unsupported or malformed serialized input now fails explicitly rather than being reinterpreted.

If a deployment persists bytecode, retain the source or build provenance needed to regenerate it and test the persisted artifact against the 1.0 reader before rollout.

## Runtime authority

Review programs that use filesystem, network, process, clock, random, or host-environment facilities. 1.0 preserves default-deny execution and whole-program capability preflight. Add only the grants the program actually needs.

Browser and `.emjweb` behavior execution is intentionally sandboxed and cannot receive native host grants.

## Practical runtime changes

Program arguments are explicit and require no host capability. Records/results and bytes/codecs are first-class runtime facilities. Host operations remain bounded and verifier-visible. Prefer the documented intrinsics and Train 21 interop contracts over ad-hoc host shims.

## Editor and tooling

Install the matching 1.0 VS Code/Cursor package together with the 1.0 toolchain so LSP/tooling metadata and language behavior stay aligned. The release metadata guard requires the editor package version to match the CMake project version.

## Web authoring

Use `.emjweb` for Emojineer-native web markup. It lowers to `emojineer.web-ir.v1` and semantic HTML. Do not embed executable HTML/JavaScript as a substitute for the native markup/behavior model.

## Before deployment

Run the five-minute tour, your project test suite, `emji check`, package synchronization/verification, and any capability-sensitive acceptance tests against the release archive rather than only a source-tree build.
