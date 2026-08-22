# Product Trains

This is a product roadmap, not an audit ladder. Each train must leave a usable language capability stacked on the existing compiler/VM spine.

## Train 1 — Sovereign Language Core

Status: implemented on the Train 1 branch.

- grapheme-aware UTF-8 lexer and canonical token identity;
- parser and explicit AST;
- arithmetic, comparisons, booleans, typed globals, assignment, input/output;
- `if`/`else` and `while`;
- `EMJBC` v1 bytecode serializer and stack VM;
- execution fuel and bounded bytecode reads;
- `run`, `compile`, `exec`, `check`, and `explain` CLI commands;
- executable examples and regression tests.

## Train 2 — Functions, Collections, and Scope

- user-defined functions, parameters, return values, call frames, locals, recursion;
- arrays and maps/records;
- lexical scopes and constants;
- collection indexing, mutation, iteration, and length;
- richer Boolean operators and error values;
- bytecode verifier for stack/call-frame safety.

## Train 3 — CER and Semantic Token Layer

- Custom Emoji Registry (CER) with documented schema;
- longest-match token trie for registered multi-grapheme sequences;
- stable token IDs and readable text aliases;
- modifier-aware token declarations;
- registry validation for collisions, rendering risk, and canonical-equivalence conflicts;
- Explain Mode backed by registry metadata rather than only hard-coded core tokens.

## Train 4 — Modules, Standard Library, and `emji`

- modules/imports and versioned package metadata;
- native Emojineer standard library for text, collections, math, files, time, data formats, networking, and deterministic utilities;
- `emji init/add/remove/lock/publish` package workflow;
- checksums, lockfile, package cache, and reproducible dependency resolution.

## Train 5 — Capability-Gated Polyglot Interop

- explicit Host Interop Layer rather than semantic dependence on host languages;
- capability table controlling filesystem/network/process/FFI access;
- optional C ABI first, followed by Python/JS/WASM adapters;
- reproducibility metadata and deterministic/sandboxed execution modes.

## Train 6 — Low-Level Emojineer

- defined memory model and typed buffers;
- Emojineer assembly/low-level instruction surface;
- verifier and sandbox boundaries;
- WASM/native ABI experiments without weakening high-level safety by default.

## Train 7 — Developer Experience

- REPL;
- formatter/linter;
- debugger and bytecode disassembler;
- LSP server;
- VS Code extension for highlighting, diagnostics, completion, hover/explain, formatting, and run/debug;
- accessibility/text-alias view and cross-font rendering tests.

## Train 8 — Native Backend

- IR boundary from Emojineer AST/bytecode semantics;
- LLVM/native compilation path;
- equivalence tests proving VM and native backends agree on observable behavior.

## Later experimental packs

- legacy/dialect packs such as the archived QBasic-inspired aliases and music commands;
- semantic-compression/macros inspired by the SCL discussion;
- bytecode packing/obfuscation that operates after parsing and never abuses presentation selectors to create source ambiguity.
