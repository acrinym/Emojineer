# Capability Model and Native Facilities

Train 20 gives Emojineer programs an explicit path to host resources without making host authority ambient.

The default execution policy grants **no filesystem, network, process, clock, randomness, or host-environment authority**. A program that contains a native host call carries its exact required capability mask in EMJBC v8, and the production VM checks the complete mask before the first instruction executes.

This is a coarse-grained capability boundary, not a claim of operating-system-grade containment. Grant only capabilities appropriate for source you trust.

## Native facilities

Native facilities use ordinary Emojineer function-call syntax, but their emoji names are reserved and lower directly to the `HostCall` bytecode instruction.

| Emoji | Facility | Capability | Signature | Result |
| --- | --- | --- | --- | --- |
| `🗂️` | `filesystem.read-text` | `filesystem` | `🗂️ 🫴 path 🤲` | UTF-8/raw text bytes read from the host file |
| `🌐` | `network.get` | `network` | `🌐 🫴 url 🤲` | HTTPS response body as text |
| `⚙️` | `process.run` | `process` | `⚙️ 🫴 command 🤲` | host command-processor status as an integer |
| `🕰️` | `clock.millis` | `clock` | `🕰️ 🫴 🤲` | millisecond clock value |
| `🎲` | `random.int` | `random` | `🎲 🫴 positive_bound 🤲` | integer in `[0, bound)` |
| `🖥️` | `host.environment` | `host` | `🖥️ 🫴 name 🤲` | environment value, or empty text when unset |

These emoji cannot be redefined as ordinary user functions. The module linker also preserves them when they occur inside imported local, package, or standard source so dependency code cannot disguise host authority as an ordinary call.

## Inspect before execution

Use `capabilities` on source or serialized bytecode:

```bash
emojineer capabilities app.emoji
emojineer capabilities app.emjbc
```

Example output:

```text
required capabilities: filesystem, clock
```

The order is canonical: `filesystem, network, process, clock, random, host`.

## Explicit grants

Execution starts with zero grants. Grant capabilities only on execution commands:

```bash
emojineer run app.emoji --grant filesystem
emojineer exec app.emjbc --grant clock --grant random
emojineer repl --grant host
emojineer debug app.emoji --grant filesystem
```

`--grant all` grants every coarse capability in normal mode.

Runtime grants are accepted only by `run`, `exec`, `repl`, and `debug`. Commands such as `check`, `compile`, `fmt`, `lint`, `dump`, `disasm`, `capabilities`, LSP/editor operations, and `emji` package/registry operations do not receive program-runtime grants.

Registry HTTPS access and authenticated publication remain separate package-manager/tooling authority. A registry operation does not grant a compiled program network access, and a program network grant does not expose `emji` credentials.

## Whole-program preflight

The compiler records the union of capabilities required by all `HostCall` instructions in the linked chunk, including calls originating in imported modules and declared dependency packages.

Before execution, the VM:

1. verifies the bytecode structurally;
2. proves that the serialized required-capability mask exactly matches the native host calls in the instruction stream;
3. validates the selected execution policy;
4. rejects any missing grant;
5. only then begins executing bytecode.

This is intentionally a **preflight** boundary. If a program prints a value and later contains a clock call, running it without `--grant clock` produces no earlier print output. Missing authority cannot be discovered only after part of the program has already executed.

The `HostCall` instruction checks its capability again at execution time as defense in depth.

## Sandbox mode

```bash
emojineer run app.emoji --sandbox
```

Sandbox mode is a hard zero-host-capability policy. Combining `--sandbox` with any `--grant` is rejected. Programs requiring host capabilities therefore fail preflight.

The sandbox switch controls Train 20 native host facilities. It does not claim to create a separate OS process/container sandbox around the compiler executable itself.

## Deterministic mode

Deterministic mode permits only the virtualizable `clock` and `random` capabilities:

```bash
emojineer run simulation.emoji \
  --deterministic \
  --grant clock \
  --grant random \
  --seed 7 \
  --clock-ms 1000
```

Rules:

- `filesystem`, `network`, `process`, and `host` grants are rejected in deterministic mode;
- `--seed` and `--clock-ms` require `--deterministic`;
- `--sandbox` and `--deterministic` are mutually exclusive;
- the deterministic clock returns the configured start value and advances by one millisecond per `🕰️` call;
- deterministic random uses a seeded VM-local generator, so the same program, seed, and inputs reproduce the same `🎲` sequence;
- VM restart resets deterministic clock/random state to the execution policy.

Deterministic mode controls Train 20 clock/random native facilities. Program input supplied through `📥` remains an explicit external input and must itself be reproduced when deterministic replay matters.

## Facility bounds and host behavior

### Filesystem

`🗂️` performs an explicit host file read, accepts only an opened regular file, and rejects files larger than 16 MiB. Special files such as FIFOs/pipes are rejected after the opened object is inspected, so path replacement cannot bypass the file-type check or turn the VM into an unbounded blocking read. The `filesystem` grant is currently coarse: it is not a path allowlist. A granted program may request paths available to the host process under ordinary OS permissions.

### Network

`🌐` accepts only bounded `https://` URLs. The current libcurl-backed implementation:

- verifies TLS peer and host;
- rejects redirects;
- rejects credential-bearing URL authorities;
- uses bounded connection/request timeouts;
- limits response bodies to 1 MiB.

If Emojineer was built without libcurl, `network.get` reports that the facility is unavailable even when the network capability was granted.

The `network` grant is currently coarse. It does not provide hostname or IP allowlisting.

### Process

`⚙️` deliberately represents broad host command-processor authority. It delegates the supplied command text to the host command processor and returns its status. The command is bounded in size, but the capability is otherwise intentionally honest about its power.

**Do not grant `process` to untrusted Emojineer source.** It is not a restricted subprocess API.

### Host environment

`🖥️` reads one environment variable after validating a portable variable name. The `host` capability is separate from registry publication credentials, but a process environment can contain sensitive values. Grant it only when that observation is intended.

## EMJBC v8 contract

Train 20 appends `HostCall` to the opcode set and adds a serialized `required_capabilities` mask after the v7 source-provenance table.

The verifier independently walks every `HostCall`, maps its native-facility operand to a capability, recomputes the exact union, and rejects bytecode when:

- the facility operand is invalid;
- the mask contains unknown bits; or
- the serialized mask does not exactly equal the capability union inferred from the instruction stream.

A bytecode producer therefore cannot suppress the runtime preflight simply by writing a false zero-capability header.

The reader remains compatible with EMJBC v1 through v7. Older bytecode contains no `HostCall` instruction and therefore has a zero required-capability mask.

## Authority model summary

- **Default:** no Train 20 host authority.
- **Source and dependencies:** capability requirements compose across the complete linked bytecode chunk.
- **Bytecode:** verifier binds authority metadata to actual host-call instructions.
- **VM:** checks the entire contract before execution and again at each host call.
- **REPL/debugger:** use the same production VM and execution policy, not alternate authority paths.
- **Tooling:** compile/check/LSP/package/registry work does not inherit program-runtime grants.
- **Deterministic mode:** virtual clock/random only.
- **Sandbox mode:** zero native host facilities.

This boundary is the prerequisite for later WASM/host interop and lower-level ABI work: new adapters should compose with this capability model rather than smuggling authority around it.
