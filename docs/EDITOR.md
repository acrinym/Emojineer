# Emojineer Editor Experience

The supported editor package lives under `editors/vscode/` and targets VS Code
and compatible editors such as Cursor. It is a client for the existing native
`emojineer-lsp`; it does not contain a second Emojineer parser.

## Install

Build a VSIX:

```bash
cd editors/vscode
npm ci
npm test
npm run package
```

Install the resulting `.vsix` through the editor's extension UI or CLI.

## Toolchain discovery

By default the extension starts `emojineer-lsp` and language commands from
`PATH`. Set `emojineer.toolchainPath` to the directory containing the three
Emojineer binaries, or set `emojineer.lspPath` to override only the server.

## Commands

- **Emojineer: Run Current File**
- **Emojineer: Check Current File**
- **Emojineer: Format Current File**
- **Emojineer: Debug Current File**
- **Emojineer: Insert Semantic Emoji**

The semantic palette is an input method: users search textual meanings such as
"function", "loop", or "return" and insert the canonical native emoji token.
Saved source remains emoji-native.

## Accessibility and semantic ownership

Textual names and descriptions appear in the semantic palette and the existing
LSP hover/completion surfaces. TextMate grammar rules provide immediate visual
presentation, while diagnostics, completion, navigation, formatting, package
awareness, and semantic meaning stay owned by the production LSP/compiler
model.
