# Emojineer Language Server

Train 17 makes Emojineer a first-class editor language through a real Language Server Protocol implementation backed by the sovereign parser, module graph, package graph, formatter, diagnostics, CER semantics, and standard library.

## Product contract

The repository ships a dedicated `emojineer-lsp` executable speaking JSON-RPC/LSP over stdio. It must use the existing compiler/parser/linker/package model rather than a parallel ad-hoc parser.

The server supports initialize/shutdown/exit, text document open/change/save/close, incremental document state, publishDiagnostics, document formatting, completion, hover/explain, definition, references, document symbols, workspace symbols, and text-document synchronization. All positions are translated correctly between LSP UTF-16 positions and Emojineer UTF-8/grapheme-aware source handling.

Diagnostics include syntax, semantic, module/import, package/direct-dependency, lock/materialization, and CER-aware failures where applicable, with stable ranges and human-readable messages. Unsaved editor buffers must participate in diagnostics and navigation without mutating project files.

Completion and hover understand core emoji tokens, active CER aliases/descriptions, user symbols, exports, standard-library imports, local modules, path packages, and materialized registry packages. Completion must not expose undeclared transitive package imports.

Go-to-definition and references cross ordinary modules and package-qualified imports while preserving package ownership boundaries. Formatter requests reuse the canonical formatter and never rewrite multiline-string contents incorrectly.

Workspace discovery follows `emojineer.toml`, lock/store state, package graph, and module graph. Ordinary LSP requests never initiate registry network access; remote packages are read only from the verified lock/materialized store already produced by package-manager operations.

The server supports deterministic textual views for emoji/CER semantics so editors and accessibility tooling can display token meaning without replacing the native emoji source model.

## Acceptance journey

Open a mixed local + remote dependency workspace in the LSP fixture, edit a source buffer without saving, receive updated diagnostics, complete a local/exported/std symbol, hover a CER-aware token, navigate definition/references across modules and a direct package dependency, reject navigation/import access to an undeclared transitive package, format the document, and verify all requests work with the registry unavailable.

## Boundaries

- No alternate parser or JavaScript/Python language-server implementation.
- No ambient package-manager network activity from LSP requests.
- No weakening package ownership/direct-dependency rules.
- No editor-specific extension required for the server core.
- No audit machinery.
