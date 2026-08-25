# Emojineer Source Debugger

Train 18 adds a real source-level debugger over the existing compiler, bytecode, VM, module graph, and package graph. It must preserve language-visible behavior and use explicit source-position metadata rather than a second evaluator.

## Product contract

The compiler emits deterministic source mappings that associate executable bytecode locations with module identity, source file, line/column range, and function context. The bytecode format/version is advanced only as needed, with prior-reader compatibility preserved where practical and explicit incompatibility diagnostics otherwise.

The VM gains a debugger control interface supporting source breakpoints, continue, pause-at-next-safe-point, step into, step over, step out, stack/call-frame inspection, locals, globals, and value rendering. Debugger observation must not mutate program state or consume program input.

A user-facing `emojineer debug <source-or-project>` session provides commands for breakpoints, continue, step/next/out, backtrace, frame selection, locals, globals, print/inspect, source listing, and quit. Breakpoints may target file:line and resolve across linked local modules, standard modules when source is available, direct path packages, and materialized registry packages.

Source mappings use the same deterministic module/package identities as ordinary compilation and do not encode checkout-specific absolute roots in bytecode or debugger protocol data. Runtime diagnostics remain meaningful when a breakpoint cannot bind or a source file has drifted from compiled bytecode.

The debugger works offline and never grants filesystem/network/process authority to the debugged program beyond ordinary Emojineer capabilities. Inspecting values and source metadata is debugger authority, not program authority.

The debugger exposes a protocol-neutral core so a later DAP/editor adapter can drive the same engine without duplicating stepping semantics.

## Acceptance journey

Debug a project whose root imports a local module and a direct package. Set breakpoints in root, module, and package source; run to each; step into a function call; step over another call; step out; inspect nested call frames, parameters, locals, globals, arrays/text values, and source location; continue to completion; repeat after compiling/executing deterministic bytecode; verify transitive package ownership and offline behavior remain intact.

## Boundaries

- No second interpreter/evaluator for debugging.
- No debugger-only semantic changes to program execution.
- No ambient host capabilities granted to debugged programs.
- No absolute checkout roots embedded in deterministic bytecode identities.
- No audit machinery.
