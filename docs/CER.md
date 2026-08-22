# Custom Emoji Registry (CER)

CER files let Emojineer projects extend the visual vocabulary without changing the language runtime. A registered sequence lowers to an existing semantic token, so custom glyph packs remain part of Emojineer's own parser/VM rather than becoming a transpiler layer.

```json
{
  "tokens": [
    {"glyph":"🤖🔥","alias":":fire_print:","description":"custom print","maps_to":"Print"}
  ]
}
```

Run with `emojineer run program.emoji --cer cer/example.json`. Multiple `--cer` files may be loaded in order.

Each token receives a deterministic 32-bit FNV-1a semantic ID over its canonical grapheme sequence. CER rejects canonical-equivalent glyph collisions, alias collisions, ID collisions, unknown fields and unknown `maps_to` targets. Variation selectors therefore cannot create shadow syntax, while skin-tone and ZWJ distinctions remain available as defined by the core Unicode contract.

The lexer uses longest-match selection, so registered multi-grapheme tokens such as `🤖🔥` are consumed as one semantic token before ordinary emoji identifiers are considered. `emojineer explain` shows each custom token's description, readable alias and stable semantic ID.
