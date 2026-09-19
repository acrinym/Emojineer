# Host / WASM Interop - Train 21

Train 21 adds Emojineer's first explicit host/WASM ABI on top of Train 20's default-deny authority model. It does not translate Emojineer into another language and does not create a second interpreter. Source still compiles to EMJBC and executes through the production VM.

## Source declarations

`🔌` declares a typed callable supplied by the embedding host or WASM runtime:

```emoji
🔌 🧮 📜fixture.double📜 📜none📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 21 🤲
```

The fields are, in order: source-level emoji name, external adapter name, capability specification, result type, and positional parameter types. Capability specifications accept `none`, `all`, or comma-separated Train 20 capability names.

`📡` exports an Emojineer function through the same typed ABI:

```emoji
📡 🚀 📜fixture.add📜 🔢 🫴 🔢 🤲
🛠️ 🚀 🫴 🍎 🤲
    📦 🍎 ➕ 1
🏁
```

The exported signature must have the same arity as the target function. Current crossing types are number (`🔢`), text (`🔤`), boolean (`🎯`), and array (`📚`).

## Compiler and EMJBC v9

Interop declarations are top-level metadata. Calls to a `🔌` name lower to `InteropCall`, while ordinary Emojineer functions continue to lower to `Call` and Train 20 native facilities continue to lower to `HostCall`.

EMJBC v9 introduced bounded interop import/export tables. The current v10 writer preserves those tables unchanged while adding unrelated 1.0 runtime opcodes/facilities. Imports carry internal/external names, required capability masks, and typed signatures. Exports carry external names, function indices, and typed signatures. The bytecode verifier checks table bounds, external-name shape/uniqueness, capability masks, signatures, export targets/arity, and every `InteropCall` operand.

## Authority and adapter preflight

A `🔌` declaration's capability mask contributes to the exact whole-program `required_capabilities` union, including calls originating in linked dependencies. The VM verifies that union and checks execution grants before instruction zero.

The embedding host binds adapters through `InteropRegistry`. A binding supplies the same external name, an exact capability mask, a deterministic flag, and a byte-in/byte-out callback. Before execution begins, the VM requires every referenced adapter to exist and requires its bound capability mask to equal the compiled declaration. A host therefore cannot bind a network-capable callback to source that declared `none`.

In deterministic execution mode, every referenced adapter must also be bound with `deterministic=true`. This is an embedding contract, not an attempt to prove callback purity.

## EMJABI1 value envelope

Host/WASM calls cross through a deterministic binary request/response envelope. The codec preserves integer versus binary64 numeric subtypes internally while the source signature's `number` family accepts either. It also carries booleans, UTF-8 text, and recursively nested arrays. The v1 ABI intentionally does not serialize the newer record, result, or bytes runtime values; attempting to place one anywhere inside an ABI array is rejected rather than silently changing EMJABI1.

Current hard bounds are:

- complete request or response: 1 MiB;
- one text value: 256 KiB;
- nesting depth: 64 levels;
- aggregate values: 100,000;
- external adapter/export name: 1 to 256 printable ASCII bytes without spaces.

Non-finite floating-point values, null arrays, malformed tags, invalid booleans, truncated envelopes, and trailing bytes are rejected. A failure response carries a bounded message and becomes an `adapter failure` runtime error.

## Export invocation

Hosts can invoke an exported function through `VM::invoke_export` using ordinary Emojineer `Value` objects or through `VM::invoke_export_abi` using an `EMJABI1` request. Both paths use the production VM and the same verifier-visible signature.

On first export invocation the VM initializes the chunk through its normal verification/preflight path, allowing module/global initialization to run once. Subsequent export calls reuse that initialized VM state. Export results are type-checked against the declared signature before being returned or encoded.

## Inspection

`emojineer interop <file.emoji|file.emjbc>` prints the verifier-visible import/export surface without executing the program. `emojineer capabilities` continues to report the complete authority union, now including capabilities required by interop calls. `disasm` shows v9 interop tables and `InteropCall` instructions.

## Train 22 EASM composition

Train 22 reuses this boundary for low-level code instead of adding a privileged host path. EASM imports invoke ordinary `InteropRegistry` bindings through `EMJABI1`, with exact capability-mask and deterministic-eligibility checks. An EASM export can be bound back into `InteropRegistry` with `bind_easm_export`; the exposed adapter's capability mask and deterministic flag are derived from the low-level imports that export actually calls.

This means high-level `🔌` calls into EASM still pass through the same Train 21 binding/preflight contract. See [EASM.md](EASM.md).

## Boundary rule

Train 21 is an explicit embedding boundary, not ambient FFI. An Emojineer program cannot discover or invoke arbitrary host functions. Only compiler-declared imports can emit `InteropCall`, only matching host bindings can satisfy them, and capability/binding preflight occurs before program effects. Train 22 EASM deliberately composes through this boundary rather than weakening it.
