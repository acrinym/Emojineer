# Browser Playground

The browser playground is a static WebAssembly build of the real Emojineer
compiler and VM. It does **not** reimplement Emojineer in JavaScript.

The browser-specific C++ entrypoint calls `run_browser_source()`, which uses the
same lexer, parser, compiler, bytecode verifier, production VM, and ICU-backed
Unicode semantics as native execution. Emscripten's ICU port ships stub data, so
the build deterministically fetches the matching ICU 68.2 source archive by
SHA-512 and preloads its `icudt68l.dat` file into the browser bundle. Execution
policy is permanently `Sandbox`, so browser programs receive zero native host
capability grants.

## Build the static bundle

Use the Emscripten SDK:

```bash
emcmake cmake -S . -B build-browser \
  -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-browser --target emojineer-playground -j 2
```

The output directory contains:

- `emojineer-playground.js` - Emscripten loader/runtime glue;
- `emojineer-playground.wasm` - compiled Emojineer compiler + VM;
- `emojineer-playground.data` - the pinned ICU 68.2 runtime data required for NFC, grapheme segmentation, and Unicode emoji properties;
- `index.html` - playground UI;
- `playground.js` - UI only, calling the exported WASM API;
- `style.css` - presentation.

Serve that directory with any ordinary static web server. No application server
is required.

## Browser behavior

The UI provides native `.emoji` source editing, curated examples, Run, stdout,
diagnostics, capability reporting, and a reproducible share link stored in the
URL fragment.

Because the source is stored in the fragment, opening a share URL does not send
the source to a server. Execution happens locally in the browser after the WASM
runtime loads.

The browser runtime intentionally rejects ambient authority. A program that
requires `filesystem`, `network`, `process`, `clock`, `random`, or `host`
capabilities fails before instruction zero rather than receiving browser or
server privileges.

## Equivalence and CI

`tests/browser_runtime_tests.cpp` compares representative browser-runtime
results against direct production-VM sandbox execution and covers explicit
program arguments plus capability denial.

`.github/workflows/browser-playground.yml` builds the actual Emscripten bundle
inside the pinned `emscripten/emsdk` image and runs `browser/smoke.cjs` against
the generated WASM runtime. The smoke test executes real Emojineer source and
proves the network capability remains denied.
