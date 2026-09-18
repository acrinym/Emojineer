# Installing Emojineer

Emojineer releases are built as self-contained toolchain archives containing
`emojineer`, `emji`, `emojineer-lsp`, documentation, and examples. A C++
compiler is not required to use a published build.

## Release artifacts

Release automation builds and tests Windows, Linux, and macOS packages from the
same source revision. CPack names archives as:

`emojineer-<version>-<os>-<architecture>.<zip|tar.gz>`

Every package receives a SHA-256 checksum. Release metadata records the source
revision and toolchain version used by CI.

## Windows portable install

Extract the ZIP and run the executables from `bin\`, or install a portable
copy for the current user:

```powershell
.\scripts\windows\install-emojineer.ps1 -Source <extracted-package> -AddToPath
emojineer --version
emji --version
```

Remove that installation with:

```powershell
.\scripts\windows\uninstall-emojineer.ps1 -RemoveFromPath
```

The installer scripts never require administrator rights when the default
per-user destination is used.

## Linux and macOS

Extract the release archive and place its `bin/` directory on `PATH`, or use
CMake's install tree when building from source. Release CI verifies the same
installed layout on every supported runner.

## Building packages locally

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure
cmake --install build-release --prefix staging
cpack --config build-release/CPackConfig.cmake
```

Local qualification does not require signing secrets. Release operators may set
`EMOJINEER_SIGN_SCRIPT` to a local executable/script accepting one artifact
path at a time, then invoke `scripts/release/invoke-signing-hook.ps1`.
