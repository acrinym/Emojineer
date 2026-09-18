# Practical 1.0 Runtime Values and Utilities

The 1.0 productization arc adds a small set of ordinary runtime values and
host-facing utilities without creating a second language model.

## Records

`🗃️` creates a named record from an array of key/value pairs:

```emoji
🐍 🧑 🟰 🗃️ 🫴 📜Person📜 📚 🫴 📜name📜 📜Ada📜 📜age📜 37 🤲 🤲
📝 🔎 🫴 🧑 📜name📜 🤲
🐍 👤 🟰 🧷 🫴 🧑 📜age📜 38 🤲
```

Record keys are bounded UTF-8 text. `🔎` reads a field, `🧷` returns a
value-style updated record, and `📏` returns the field count. `🗝️` returns
keys in deterministic lexical order; `🏷️` returns the record type name.

## Explicit results

`🟢 value` and `🔴 value` create explicit success/error results.
`👌 result` tests success and `🎁 result` returns the payload.

These are values rather than exceptions. Runtime contract violations such as an
out-of-range collection access remain runtime errors; application-level
recoverable states can be represented explicitly with results.

## Bytes and encoding

`🧬 text` UTF-8 encodes text to bounded bytes. `🗣️ bytes` decodes bytes and
rejects malformed UTF-8. `🔡`/`🔣` perform hexadecimal encoding/decoding;
`📨`/`📩` perform canonical Base64 encoding/decoding.

Bytes use existing collection operations:

- `🔎` returns a byte as an integer in 0..255;
- `📏` returns byte length;
- `📎` appends one checked byte and returns a new bytes value;
- `🧷` replaces one checked byte and returns a new bytes value.

Byte values are bounded to 16 MiB.

## Program arguments

`🧳 🫴 🤲` returns the explicit argument array supplied after `--`:

```bash
emojineer run app.emoji -- alpha "two words"
emojineer exec app.emjbc -- alpha "two words"
```

Arguments are explicit invocation input, not ambient host authority, so reading
them does not require a capability grant and remains compatible with sandboxed
or deterministic execution.

## Practical host facilities

The existing capability model gains:

- `✍️ path text` -> `filesystem.write-text`;
- `📁 path` -> `filesystem.create-directory`;
- `🛰️ method url body` -> `network.request`.

Filesystem operations require the existing `filesystem` grant. Network
requests require the existing `network` grant. They participate in whole-
program preflight exactly like the Train 20 facilities.

`network.request` accepts GET, POST, PUT, PATCH, and DELETE over HTTPS only.
GET requires an empty body. Request and response bodies are bounded to 1 MiB,
TLS peer/host verification remains enabled, and redirects remain disabled.

## Bytecode

These runtime additions use EMJBC v10. The v10 writer preserves all v9 typed
interop tables and adds verifier-visible `IntrinsicCall` plus the appended
host facility operand identities. Readers continue to accept older versions;
v8/v9 bytecode cannot smuggle v10-only host facility operands.
