# EMJBC Bytecode Specification

`EMJBC` is Emojineer's owned bytecode format. It is not Python bytecode, JavaScript, JVM bytecode, WebAssembly, or a serialization of host-language source.

The current writer emits **version 3**. The reader accepts versions **1, 2, and 3**.

## Container header

Every bytecode stream begins with the five ASCII magic bytes:

```text
EMJBC
```

followed by a little-endian unsigned 16-bit format version.

All multi-byte integer fields are explicitly serialized little-endian.

## Safety limits

The current reader/verifier enforces these format limits before large allocations or execution:

- constants: 1,000,000 maximum;
- functions: 100,000 maximum;
- instructions: 10,000,000 maximum;
- one serialized string: 64 MiB maximum.

Malformed, truncated, oversized, unknown-version, or structurally invalid bytecode is rejected.

## Version history

### Version 1

The original stack-machine format: constants, globals, runtime type assertions, arithmetic/comparison, input/output, control-flow jumps, and halt. Version 1 does not contain a function metadata table.

### Version 2

Adds functions, parameters, local slots, call frames, `Call`, and `Return`. The reader retains explicit v1 opcode decoding so old v1 files remain meaningful after the enum grew.

### Version 3

Adds first-class collection instructions: array type assertion, construction, indexing, length, append, and value-style element replacement.

Train 8 modules do **not** require version 4. Module linking resolves source-unit visibility and rewrites symbols to deterministic internal names before ordinary v3 bytecode generation.

## Constant pool

After the version, the stream stores an unsigned 32-bit constant count followed by tagged constants.

Current tags:

| Tag | Value |
| ---: | --- |
| 1 | IEEE-754 binary64 number, serialized as its 64-bit bit pattern |
| 2 | boolean, followed by one byte `0` or `1` |
| 3 | UTF-8 string, length-prefixed by unsigned 32-bit byte count |
| 4 | signed 64-bit integer bit pattern |

Arrays are runtime values and are deliberately not stored directly in the bytecode constant pool.

## Function table

Versions 2 and later store an unsigned 32-bit function count. Each function record contains:

1. UTF-8 function name;
2. unsigned 32-bit entry instruction index;
3. unsigned 32-bit arity;
4. unsigned 32-bit local-slot count.

The verifier requires each function entry to point inside the instruction stream, requires arity not to exceed local count, and rejects duplicate function metadata names.

Train 8 module-linked functions may appear here under deterministic internal names such as:

```text
@module/lib/math.emoji::🧠
```

That string is linker metadata, not source syntax.

## Instruction stream

The stream stores an unsigned 32-bit instruction count. Every serialized instruction is fixed-width:

```text
u8 opcode
u32 operand_bits
u32 source_line
```

`operand_bits` is the bit representation of the instruction's signed 32-bit operand. `source_line` is retained for diagnostics.

## Current opcode set

The in-memory v3 instruction set is:

| Opcode | Purpose |
| --- | --- |
| `Constant` | push constant-pool value |
| `LoadGlobal` / `StoreGlobal` | read/write named global |
| `LoadLocal` / `StoreLocal` | read/write current call-frame local |
| `AssertNumber` | runtime number type assertion |
| `AssertString` | runtime text type assertion |
| `AssertBool` | runtime boolean type assertion |
| `Add` / `Subtract` / `Multiply` / `Divide` / `Modulo` | numeric operations; `Add` also supports two text values |
| `AddInt` / `SubtractInt` / `MultiplyInt` | checked signed-integer operations used by bytecode/runtime support |
| `Equal` / `Less` / `Greater` | comparison |
| `Negate` / `Not` | unary numeric negation / boolean NOT |
| `ReadLine` / `Print` | stdin/stdout I/O |
| `JumpIfFalse` / `Jump` | control flow |
| `Call` / `Return` | function calls and returns |
| `Halt` | terminate the program |
| `AssertArray` | runtime array type assertion |
| `MakeArray` | construct an array from stack values |
| `Index` | read array element |
| `Length` | array size or Unicode-grapheme-aware text length |
| `Append` | return an array with an appended value |
| `SetIndex` | return an array with one replaced element |

## Verification

`verify_bytecode` checks structural safety independently of source parsing. Among other checks it validates:

- safety-limit counts;
- absence of arrays in the constant pool;
- function metadata integrity;
- constant operands;
- global operands referencing string constants;
- nonnegative local slots;
- jump targets;
- function-call indices;
- nonnegative array construction counts.

The VM verifies a chunk again before execution.

## VM execution model

The VM uses:

- an operand stack;
- a global-value map;
- a call-frame stack for function locals and return instruction pointers;
- a configurable execution-fuel budget, defaulting to 1,000,000 instructions;
- stdin and stdout references supplied by the host executable.

Maximum call depth is currently 4096 frames.

A `Halt` reached while call frames remain, or with leaked values on the operand stack, is a runtime error. A function return also checks that its operand-stack segment did not leak values.

## Value behavior

Runtime values currently include numbers, signed integers used by bytecode support, booleans, UTF-8 text, and arrays. Array equality is structural and recursive. Array append/replacement operations use value-style semantics: the prior array value is preserved and a transformed array is returned.

Text length counts Unicode extended grapheme clusters through the same ICU-based Unicode machinery used by the language.

## Compatibility rule

A bytecode-format bump is required only when serialized representation or opcode compatibility requires it. Source-language growth alone is not a reason to increment EMJBC. Train 8 modules are the current example: they add substantial language semantics while continuing to emit ordinary v3 chunks.
