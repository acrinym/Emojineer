# Package Artifacts and Registry Transport

Emojineer 0.14 turns the immutable `.emjpkg` substrate from Train 13 into a real package-exchange workflow. The transport layer now has registry identity/discovery, immutable package-version indexes, local-registry publication, SemVer selection, verified fetching, registry-scoped content-addressed caching, and optional HTTPS read transport.

This is still deliberately separate from project dependency manifests. A fetched artifact does not silently become a dependency. Remote dependency declarations and deterministic remote lock provenance need to land together in the next train.

## `.emjpkg` artifacts

`emji pack` creates deterministic binary `.emjpkg` artifacts with format marker `EMJPKG1`.

```text
emji pack
emji pack path/to/package
emji pack path/to/package -o dist/package.emjpkg
```

Artifacts contain package identity, canonical manifest text, package-owned `.emoji` source records, per-source SHA-256 values, package `content-sha256`, and a separate whole-artifact `artifact-sha256`.

The parser remains non-extracting and bounded. It rejects oversized artifacts, malformed framing, noncanonical manifest metadata, invalid source paths/order, checksum mismatches, missing entry source, content-identity mismatches, and trailing bytes.

## Registry endpoint contract

Supported endpoint kinds are:

```text
/path/to/registry
file:///absolute/path/to/registry
https://registry.example/api
```

Plain `http://` network endpoints are rejected. HTTPS endpoint host names are canonicalized to lowercase and trailing endpoint slashes are removed. Query strings, fragments, user-info syntax, backslashes, whitespace, and `.` / `..` path segments are rejected.

Local paths are normalized to absolute `file://` identities internally.

HTTPS read transport is compiled when CMake finds libcurl. A build without libcurl keeps complete local-registry behavior and reports that HTTPS transport is unavailable if an HTTPS endpoint is used.

## Registry identity and discovery

Initialize a local registry with:

```text
emji registry-init ./registry --id local.dev
```

The registry stores a bounded discovery descriptor at:

```text
v1/registry.txt
```

with format marker:

```text
EMJREGISTRY1
id=local.dev
```

The registry ID is immutable for an initialized registry. Package indexes carry the same ID and are rejected if they claim a different registry identity.

Inspect an endpoint with:

```text
emji registry-info --registry ./registry
emji registry-info --registry https://registry.example/api
```

## Package-version indexes

Each package has one canonical index:

```text
v1/packages/<package>.index
```

using the `EMJREGPKG1` format. Every immutable version record binds:

- exact SemVer text;
- package `content-sha256`;
- exact `.emjpkg` `artifact-sha256`.

Records are rendered in deterministic textual version order and duplicate version identities are rejected.

Browse versions with:

```text
emji versions my_package --registry ./registry
```

The existing Train 13 SemVer engine chooses the highest matching version for `*`, exact, caret, and tilde requirements. Wildcard and ordinary ranges exclude prereleases by default; explicit prerelease requirements retain the existing SemVer rules.

## Immutable publication

Publish a package to a writable local registry with:

```text
emji publish path/to/package --registry ./registry
```

Publication:

1. builds the deterministic `EMJPKG1` artifact;
2. verifies it through the ordinary artifact parser;
3. stores the artifact by exact artifact SHA-256 at `v1/artifacts/sha256/<hash>.emjpkg`;
4. adds the package/version record to the canonical package index.

Re-publishing exactly the same package/version/artifact is idempotent. Publishing different content under an already-published package/version is rejected as an immutable version conflict.

HTTPS publication is intentionally not implemented yet. Uploading needs an authenticated immutable-write protocol, authorization model, and server-side conflict semantics rather than a generic HTTP PUT hidden behind the CLI.

## Verified fetch and cache admission

Fetch a package artifact by SemVer requirement with:

```text
emji fetch my_package '^1.2.0' --registry ./registry
emji fetch my_package '~1.4.0' --registry https://registry.example/api
```

An explicit cache root can be supplied:

```text
emji fetch my_package '^1.2.0' --registry ./registry --cache ./cache
```

Without `--cache`, the platform cache root is used:

- Windows: `%LOCALAPPDATA%/Emojineer/cache` when available;
- Unix-like systems: `$XDG_CACHE_HOME/emojineer` or `$HOME/.cache/emojineer`;
- otherwise a temporary-directory fallback.

Fetch performs these checks before cache admission:

1. load and verify the registry descriptor;
2. load the package index and require matching registry/package identity;
3. select the highest matching version deterministically;
4. retrieve the exact artifact named by the index artifact SHA-256;
5. parse/verify the complete `.emjpkg` artifact;
6. require package name, exact version, content SHA-256, and artifact SHA-256 to match the selected index record;
7. only then write the artifact to the content-addressed cache.

The cache is scoped by a digest of canonical endpoint identity plus registry ID, then by package/version/artifact hash. A cache hit is re-verified before reuse. If a cached artifact is malformed or does not match the selected index record, it is discarded and fetched again.

## HTTPS trust boundary

When libcurl is available, HTTPS registry reads use TLS certificate and host verification, disallow redirects, use bounded response buffers, and apply connection/overall request timeouts. Only `https://` is allowed through the network transport.

HTTPS authenticates the registry server through TLS. It does **not** provide package-author signatures, transparency logs, or authenticated client publication. Those are distinct trust mechanisms and are not implied by TLS.

## Two SHA-256 identities

`content-sha256` identifies package-owned semantic/source content using the existing `PackageGraph` content identity.

`artifact-sha256` identifies the exact serialized `.emjpkg` bytes.

A registry index binds both. Fetch is successful only if both identities agree with the retrieved artifact.

## Current boundary and next train

Emojineer 0.14 can exchange immutable package artifacts through a real registry protocol, but `emojineer.toml` still contains local/path dependencies only. This is intentional.

The next package train should add remote dependency integration as one coherent system:

- manifest source syntax carrying registry endpoint/identity plus version requirement;
- recursive registry dependency resolution;
- deterministic materialization into a verified package store;
- lockfile records carrying registry identity, requirement, selected version, content SHA-256, artifact SHA-256, and dependency edges;
- package-aware linking against the verified materialized graph;
- real remote `emji add` only after the manifest/lock/resolver path is complete;
- authenticated HTTPS publication only after an explicit upload/authorization contract is defined.

The language runtime itself receives no ambient network or filesystem authority from registry support. Registry networking belongs to the `emji` package-management toolchain.
