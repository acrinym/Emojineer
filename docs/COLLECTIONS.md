# Arrays and collection operations

Train 4 adds first-class array values. Arrays use value-style collection transforms: append and replacement return new arrays instead of mutating existing aliases.

```emoji
🐍 🧺 📚 🟰 📚 🫴 2 4 6 🤲
📝 📏 🧺
📝 🔎 🫴 🧺 1 🤲
🐍 🎒 📚 🟰 📎 🫴 🧺 8 🤲
🐍 🧰 📚 🟰 🧷 🫴 🎒 0 99 🤲
```

- `📚 🫴 ... 🤲` creates an array literal. `📚` is also the optional array type marker in a `🐍` declaration.
- `🔎 🫴 array index 🤲` reads an element. Indexes are zero-based whole numbers.
- `📏 value` returns array length, or Unicode grapheme count for text.
- `📎 🫴 array value 🤲` returns a new array with the value appended.
- `🧷 🫴 array index value 🤲` returns a new array with one element replaced.

Arrays may contain mixed Emojineer values, including other arrays. Printing renders arrays recursively. Equality compares arrays structurally. Out-of-range access is a runtime error.

Collection instructions are encoded in EMJBC v3. The bytecode reader remains compatible with v1 and v2 artifacts.
