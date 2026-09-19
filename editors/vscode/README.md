# Emojineer for VS Code and Cursor

This extension packages the existing native `emojineer-lsp` rather than
reimplementing Emojineer's parser or semantics in JavaScript.

## Features

- production LSP diagnostics, completion, hover, navigation, symbols, formatting;
- `Run`, `Check`, `Format`, and `Debug` commands;
- searchable **Semantic Palette** that inserts canonical emoji tokens by meaning;
- TextMate presentation for core syntax while semantic understanding remains LSP-backed;
- accessibility-friendly textual names/descriptions in the palette and LSP hover.

Set `emojineer.toolchainPath` when the binaries are not already on `PATH`.
`emojineer.lspPath` overrides only the language-server executable.
