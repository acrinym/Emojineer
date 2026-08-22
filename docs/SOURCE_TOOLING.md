# Train 6 — Canonical Formatting and Source Diagnostics

Train 6 adds source-level developer tooling without changing Emojineer runtime semantics.

## Format source

```text
emojineer fmt program.emoji
emojineer fmt program.emoji -o program.emoji
emojineer fmt program.emoji --cer cer/example.json
```

The formatter:

- normalizes line endings to LF;
- applies four-space indentation to `🤔`, `🙅`, `🔁`, `🛠️`, and `🏁` block structure;
- preserves comments instead of reconstructing source from a comment-free token stream;
- does not rewrite whitespace on multiline-string-sensitive lines;
- understands CER tokens through the ordinary lexer lowering path, so custom tokens mapped to block semantics participate in indentation.

Formatting is deterministic and idempotent.

## Diagnose source style

```text
emojineer lint program.emoji
```

`lint` compares the source against canonical formatting and reports line-oriented diagnostics. It exits successfully when the file is canonical and returns a nonzero status when formatting differs or the file is missing its final newline.

This is ordinary language tooling. It does not introduce an audit subsystem or runtime policy layer.
