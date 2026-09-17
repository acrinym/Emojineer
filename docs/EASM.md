# Low-level Emojineer / EASM - Train 22

Train 22 adds `EASM1`, a verified low-level representation for explicit scalar registers, bounded typed buffers, control flow, and typed host imports. EASM is not a second high-level Emojineer language and does not bypass Train 20/21 authority. High-level `.emoji` source still compiles to EMJBC and runs on the production Emojineer VM.

EASM is currently a textual `.easm` representation. Train 22 does **not** change EMJBC, so the current EMJBC writer remains version 9.

## File structure

Every file starts with:

```text
EASM1
```

A line whose first non-whitespace character is `#` is a comment. `#` inside a token remains ordinary data, preserving the full Train 21 printable-ASCII external-name contract and canonical dump/reparse behavior.

Top-level declarations are buffers, imports, functions, and exports. `#` starts a line comment. Functions end with `end`.

```text
buffer scratch i64 4
func main () -> i64 regs (i64,i64,i64)
  i64.const r0 0
  i64.const r1 21
  buffer.store.i64 scratch r0 r1
  buffer.load.i64 r2 scratch r0
  i64.add r2 r2 r1
  return r2
end
export main main
```
## Scalar registers and buffers

Register types are `i64`, `f64`, and `bool`. A function declares its parameter types, result type, and complete register file:

```text
func choose (i64) -> i64 regs (i64,i64,bool)
```

Parameters occupy the matching leading register slots. Reading an uninitialized register is a runtime error. `i64` arithmetic is checked for overflow. Floating operations reject non-finite results and division by zero.

Buffer element types are `u8`, `i64`, and `f64`:

```text
buffer bytes u8 4096
buffer counters i64 128
buffer samples f64 1024
```

All buffer indices are `i64` registers and are bounds checked. `u8` stores accept only 0 through 255. Buffers are zero-initialized when an `EasmVM` first uses a program and persist across later export calls on that VM until `reset_memory()` or a different program is used.

Current hard limits are 1,024 buffers, 10,000 imports, 10,000 functions, 10,000 exports, 4,096 registers per function, 1,000,000 instructions per function, 16 MiB per buffer, and 64 MiB total EASM memory.

## Instructions

Constants and movement: `i64.const`, `f64.const`, `bool.const`, and `move`.

Integer arithmetic/comparison: `i64.add`, `i64.sub`, `i64.mul`, `i64.eq`, and `i64.lt`.

Floating arithmetic/comparison: `f64.add`, `f64.sub`, `f64.mul`, `f64.div`, `f64.eq`, and `f64.lt`.
Typed memory: `buffer.load.u8`, `buffer.store.u8`, `buffer.load.i64`, `buffer.store.i64`, `buffer.load.f64`, and `buffer.store.f64`.

Control flow: `label`, `jump`, `jump_if_false`, and `return`. Jumps resolve to verifier-checked instruction targets. Execution consumes instruction fuel; the default `EasmVM` budget is 1,000,000 instructions.

External calls use `call` and may target only declared imports. EASM currently has no low-level instruction for arbitrary process memory, raw pointers, direct syscalls, or direct high-level VM access.

Numeric source tokens are parsed strictly and must be consumed completely. Canonical `f64.const` output uses enough locale-independent precision to reproduce the exact binary64 value when reparsed.

## Imports and authority

An import declares an internal name, Train 21 external adapter name, capability specification, and typed signature:

```text
import remote fixture.net network (i64) -> i64
```

Capability specifications use the same `none`, `all`, or comma-separated capability names as Train 21 interop. The verifier recomputes the EASM program capability union from actual `call` instructions and rejects inconsistent constructed metadata.

Before an exported function executes, `EasmVM` computes the capabilities and adapters actually referenced by that function. It then requires the execution grants, registry binding, exact capability-mask match, and deterministic eligibility before any instruction or adapter effect occurs.

EASM does not gain independent host facilities. A `call` crosses through the existing `InteropRegistry` and deterministic `EMJABI1` request/response codec.

## Exports and the high-level ABI

Exports bind an external name to a low-level function:

```text
export easm.double double
```
`EasmVM::invoke_export` accepts typed low-level scalar values directly. `EasmVM::invoke_export_abi` accepts and returns `EMJABI1` envelopes.

`bind_easm_export` exposes one EASM export as an ordinary Train 21 `InteropRegistry` binding. The adapter capability mask is derived from the imports reached by that export. Deterministic eligibility is also derived: a pure export is deterministic, while an export that calls adapters is deterministic only when every referenced binding is marked deterministic.

That lets high-level Emojineer call low-level code without a privileged side door:

```emoji
🔌 🧮 📜easm.double📜 📜none📜 🔢 🫴 🔢 🤲
📝 🧮 🫴 21 🤲
```

The host binds `easm.double` to the EASM export. The outer Train 21 VM still performs its normal capability/binding preflight before executing the `.emoji` program.

EASM `i64` and `f64` both map to the Train 21 `number` ABI family. An EASM `i64` input accepts an integer subtype or a finite, integral, signed-64-bit-range binary64 value. An EASM `f64` input accepts binary64 directly or an integer only when conversion is exact. `bool` maps to the Train 21 boolean ABI type.

`bind_easm_export` takes shared ownership of the supplied `EasmVM` and stores an immutable copy of the `EasmProgram`, so those two objects cannot dangle behind the long-lived Train 21 adapter callback. If that `EasmVM` was constructed with a nested `InteropRegistry`, the host still owns that registry and must keep it alive while low-level imports may execute.

## CLI

```bash
emojineer easm-check program.easm
emojineer easm-dump program.easm
emojineer easm-info program.easm
emojineer easm-run program.easm
```

`easm-check`, `easm-dump`, and `easm-info` are inspection commands and reject execution-policy flags. `easm-run` invokes the external export named `main` with no arguments and accepts the ordinary execution-policy options. The CLI intentionally provides no ambient adapter registry, so a `main` function that calls an import fails unless embedded by a host that supplies the explicit binding.

`easm-dump` renders canonical reparsable `EASM1`. `easm-info` reports buffers, imports, exports, and capability requirements without executing low-level code.
## Boundary rule

EASM is a low-level execution surface owned by Emojineer, not native machine code and not ambient FFI. Typed buffers cannot be passed out as raw pointers. Imports cannot discover host functions. High-level and low-level code exchange only the scalar values declared by the verifier-visible signatures and encoded through `EMJABI1` when crossing the Train 21 boundary.

This keeps Train 22 inside the authority architecture established by Trains 20 and 21 while providing a lower-level target suitable for later backend/compiler work.
