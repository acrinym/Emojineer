# EMJBC Bytecode Specification

`EMJBC` is Emojineer's owned bytecode format. It is not Python bytecode, JavaScript, JVM bytecode, WebAssembly, or serialized host-language source.

The current writer emits **version 8**. The reader accepts **versions 1 through 8**.

## Header and scalar encoding

Every stream begins with the five ASCII bytes `EMJBC`, followed by a little-endian unsigned 16-bit format version. Multi-byte integer fields are serialized little-endian.

Safety limits include:

- constants: 1,000,000;
- functions: 100,000;
- instructions: 10,000,000;
- one serialized string: 64 MiB;
- source-provenance identities: 1,000,000.

Malformed, truncated, oversized, unknown-version, or structurally invalid bytecode is rejected before execution.

## Compatibility history

- **v1**: original stack machine, globals, assertions, arithmetic/comparison, I/O, jumps, halt. The reader retains an explicit v1 opcode decoder.
- **v2**: function table, parameters/locals, call frames, `Call`, and `Return`.
- **v3**: first-class collection instructions through `SetIndex`.
- **v4**: retained reader-compatible format generation in the historical evolution between collections and debugger metadata; no v8-only host opcode is accepted when reading it.
- **v5**: serialized function parameter/local name metadata used by tooling/debug inspection.
- **v6**: deterministic per-instruction source map with source identity, exact source range, and function context.
- **v7**: sorted source SHA-256 provenance table used for debugger source-drift detection.
- **v8**: `HostCall` plus an exact serialized required-capability mask bound by the verifier to the actual native facilities present in the instruction stream.

Older bytecode cannot contain `HostCall`, so its required capability mask is zero.

## Constants

The stream stores a `u32` constant count and tagged values:

| Tag | Value |
| ---: | --- |
| 1 | IEEE-754 binary64 number |
| 2 | boolean byte `0` or `1` |
| 3 | length-prefixed UTF-8 string |
| 4 | signed 64-bit integer bit pattern |

Arrays remain runtime values and are not valid constant-pool entries.

## Function table

Versions 2+ store function name, entry instruction index, arity, and local-slot count. Versions 5+ additionally store parameter and local names. The verifier rejects out-of-range entries, arity/local inconsistencies, malformed name metadata, and duplicate function names.

Linked functions can use deterministic internal names such as `@module/lib/math.emoji::🧠`; those are linker metadata, not source syntax.

## Instruction stream

Each serialized instruction contains:

```text
u8  opcode
u32 signed_operand_bits
u32 source_line
```

The current in-memory set includes constants, globals/locals, type assertions, arithmetic/comparison, unary operations, stdin/stdout, jumps, calls/returns, halt, collections, and v8 `HostCall`.

`HostCall.operand` is a closed native-facility identifier. The current facilities are filesystem read, HTTPS GET, process execution, clock milliseconds, random integer, and host environment lookup. See [CAPABILITIES.md](CAPABILITIES.md).

## v6 source map

The writer stores one deterministic source-map record per instruction. Each record contains source identity, start/end line and column, and function context. The verifier rejects absolute checkout-specific identities, zero/reversed source ranges, and source-map cardinality that does not match the instruction stream.

## v7 source provenance

The writer sorts source identities and stores SHA-256 values for compiled sources. The verifier bounds the table, requires portable identities, and validates digest shape. This supports debugger stale-source diagnostics without embedding checkout roots.

## v8 capability contract

After the v7 provenance table, v8 stores a `u32 required_capabilities` mask.

`verify_bytecode` independently walks all `HostCall` instructions, validates each facility operand, maps it to its capability, and recomputes the exact union. Verification fails when the serialized mask contains unknown bits or differs from the inferred union.

The VM verifies again and performs whole-program capability preflight before executing instruction zero. Serialized metadata therefore cannot lie about a host call merely to bypass the runtime grant check.

## Structural verification

The verifier checks, among other invariants:

- all safety-limit counts;
- no arrays in the constant pool;
- function metadata integrity;
- constant and global-string operands;
- nonnegative local slots;
- jump targets;
- function-call indices;
- nonnegative array construction counts;
- valid native facility operands;
- exact capability-mask/instruction agreement;
- source map and provenance invariants.

## VM execution model

The VM uses an operand stack, global map, call-frame stack, configurable instruction fuel (default 1,000,000), host-provided stdin/stdout, and an explicit Train 20 `ExecutionPolicy`.

Maximum call depth is 4096. `Halt` with live call frames or leaked stack values is an error; function returns also reject operand-stack leakage.

Default execution grants no native host capabilities. REPL and debugger execution use the same production VM policy. Deterministic mode virtualizes only Train 20 clock/random facilities; sandbox mode grants none.

## Compatibility rule

EMJBC advances when serialized representation or opcode compatibility requires it. Source-only language growth does not require a bytecode bump. Train 8 modules, for example, were resolved by the source linker without adding an opcode; Train 20 requires v8 because host calls and their verifier-bound authority contract are serialized semantics.
