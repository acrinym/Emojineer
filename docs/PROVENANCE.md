# Design Provenance

Emojineer was originally designed in conversations archived in `acrinym/AMrepo`. This repository turns those conversations into a maintained implementation.

## Primary sources

- Main design conversation: `Chats/ChatGPT-Emoji_programming_language.md` at AMrepo commit `defdd954099e5240d5502a213b4aea0fe61a18d2`.
- Consolidated language/toolchain note: `ideas/apps/direct/Emojineer_Language_Toolchain.md` at the same commit.
- Related semantic-compression discussion: `Chats/ChatGPT-Relevance-Based_AI_Rewiring.md`, especially the SCL discussion around the originally referenced lines 443-533.

## Durable requirements recovered from the archive

1. **A real language, not a syntax skin.** Emojineer owns its grammar, AST, bytecode, VM, standard-library semantics, and evolution path.
2. **UTF-8 `.emoji` source.** Emoji are language syntax, not comments decorating another language.
3. **Grapheme-aware lexing.** Extended grapheme clusters, variation selectors, ZWJ sequences, and modifiers must be handled deliberately.
4. **Canonical token identity.** Editors must not change semantics merely by inserting a text-vs-emoji variation selector.
5. **Explainability.** The lexer/toolchain can render emoji source back into plain-English token descriptions for accessibility and debugging.
6. **Sovereign bytecode first.** Native/LLVM compilation can come later without replacing the project's bytecode/VM contract.
7. **Extensibility.** The archive proposes custom emoji registries, aliases, package management, modifier-aware vocabulary, safe polyglot interop, low-level facilities, dialect packs, REPL/debugger/LSP/editor tooling, and semantic compression.
8. **Capability boundaries.** Later host interop should be explicit and capability-gated rather than becoming a hidden escape hatch from language semantics.

## Resolved archive ambiguities

The earliest sketches were intentionally provisional and reused some symbols for multiple meanings. v0.1 therefore freezes only a coherent subset.

- The early sketch used `📜` both as the string type and the string-literal fence. v0.1 retains `📜...📜` for literals and uses `🔤` for the text type.
- Presentation selectors U+FE0E/U+FE0F are canonicalized away. They cannot be used for obfuscation or to create distinct tokens.
- Skin-tone modifiers and ZWJ sequences are preserved as part of canonical identity, leaving the modifier/custom-token space available without making editor presentation choices semantic.
- C#/.NET was discussed as a fast cross-platform bootstrap. The sovereign core is implemented in C++20 so the VM/bytecode layer has no .NET runtime dependency. Future C#/Avalonia tooling remains compatible with this architecture.

When later archive ideas conflict with canonical source stability, canonical source stability wins unless the language specification is deliberately versioned.
