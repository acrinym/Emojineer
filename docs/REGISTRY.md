# Package Artifacts and Registry Foundation

Emojineer 0.13 defines the immutable package artifact and version-selection contracts that a future network registry must use. This train deliberately does **not** add fake network commands. `emji publish` and remote `emji add` remain absent until registry transport, authentication, indexes, retrieval, and remote dependency locking are implemented against this substrate.

## `.emjpkg` artifacts

`emji pack` creates a deterministic binary `.emjpkg` artifact with format marker `EMJPKG1`.

```text
emji pack
emji pack path/to/package
emji pack path/to/package -o dist/package.emjpkg
```

Without `-o`, the artifact is written inside the package root as:

```text
<name>-<version>.emjpkg
```

A package version used for distribution must be a valid SemVer 2.0 version.

The artifact contains:

- package name;
- semantic version;
- entry source path;
- the canonical `emojineer.toml` text;
- package content SHA-256;
- sorted package-owned `.emoji` source records;
- each source path, source SHA-256, and exact source bytes.

All variable-length fields are length-framed rather than delimiter-parsed. The reader bounds the total artifact to 128 MiB, individual fields to bounded sizes, and the source record count to a finite maximum.

## Source ownership

Artifact source ownership is the same ownership used by the real `PackageGraph` content hash.

If package `app` contains a resolved dependency at `deps/lib`, `.emoji` files owned by `lib` are not copied into `app`'s artifact and do not affect `app`'s package content identity. Independently resolved nested package roots are excluded the same way.

Source paths inside an artifact are portable relative paths using `/`. Absolute paths, `..` escapes, backslashes, non-`.emoji` source records, duplicate paths, and non-canonical path ordering are rejected.

Equivalent package checkouts at different absolute filesystem roots therefore produce identical artifact bytes when their package-owned manifest and source bytes are identical.

## Two SHA-256 identities

Emojineer intentionally carries two different SHA-256 identities.

### Package content SHA-256

`content-sha256` identifies the canonical package meaning already used by the local package resolver:

```text
SHA256(
  framed "EMOJINEER-PACKAGE-v1"
  + framed canonical manifest
  + framed sorted package-owned source path/source pairs
)
```

The artifact builder independently computes this value and requires it to equal the root package identity produced by `PackageGraph`. A disagreement is an internal contract failure and packing stops.

### Artifact SHA-256

`artifact-sha256` is SHA-256 over the complete serialized `.emjpkg` bytes.

This is the transport/cache identity. The planned cache layout is content-addressed by exact artifact bytes:

```text
<cache>/<package>/<version>/<artifact-sha256>.emjpkg
```

A registry index must eventually bind a package version to both the expected package content SHA-256 and exact artifact SHA-256. Retrieval is not successful until both identities verify.

## Inspect and verify

```text
emji artifact package.emjpkg
emji verify-artifact package.emjpkg
```

`artifact` parses and verifies the artifact, then prints its package metadata and both hashes.

`verify-artifact` performs the same strict parse/checksum validation and reports success only after:

- format framing is valid;
- package/version/entry metadata is valid;
- metadata agrees with the embedded canonical manifest prefix;
- source paths are canonical and sorted;
- every source SHA-256 matches its source bytes;
- the recomputed package content identity matches `content-sha256`;
- no trailing bytes remain.

The reader does not extract files into the host filesystem. Parsing an artifact therefore does not grant filesystem execution or overwrite authority.

## Semantic version requirements

The registry foundation implements deterministic SemVer parsing, precedence, requirement matching, and highest-compatible selection.

Supported requirement forms are:

```text
*        any stable release
1.2.3    exact version identity
^1.2.3   compatible caret range
~1.2.3   compatible patch line
```

Caret upper bounds follow the first non-zero component:

- `^1.2.3` allows `1.x` at or above `1.2.3`, but not `2.0.0`;
- `^0.2.3` allows `0.2.x` at or above `0.2.3`, but not `0.3.0`;
- `^0.0.3` allows only the `0.0.3` precedence line.

`~1.2.3` stays within `1.2.x` at or above `1.2.3`.

Wildcard and ordinary stable ranges do not select prereleases by default. A prerelease range may match prereleases on the same major/minor/patch core and the corresponding release. Build metadata does not affect SemVer precedence, but when two available versions have equal precedence the full version text is used as a deterministic tie-break. Exact requirements preserve the exact version text, including build metadata.

## Registry transport boundary

A future network registry must build on these contracts rather than redefine them. The next train needs to add, together:

- canonical HTTPS registry identity/discovery;
- package-version indexes that advertise immutable artifact identities;
- authenticated and bounded retrieval;
- verified content-addressed caching;
- manifest syntax for registry version requirements while preserving local/path dependencies;
- deterministic lock records containing registry identity, selected version, content SHA-256, and artifact SHA-256;
- real remote `emji add` behavior only after those pieces exist;
- publication only with an explicit authenticated immutable-upload contract.

Until that transport exists, Emojineer 0.13 has no `publish`, remote `add`, download, or network-registry command.
