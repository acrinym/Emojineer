# Package Artifacts, Registry Transport, and Discovery

Emojineer's package registry is an immutable package-exchange system layered on deterministic `.emjpkg` artifacts. Registry tooling belongs to `emji`; ordinary language execution, LSP requests, and debugger operation do not receive ambient registry/network authority.

## Immutable `.emjpkg`

`emji pack` emits deterministic `EMJPKG1` artifacts containing the canonical package manifest and package-owned `.emoji` sources. Per-source checksums, package `content-sha256`, and whole-artifact `artifact-sha256` are verified before an artifact is admitted to package state.

## Registry identity and endpoints

Supported registry endpoints are local/file paths and HTTPS. Plain HTTP is rejected.

Every registry exposes bounded identity at:

```text
v1/registry.txt
```

using `EMJREGISTRY1`. HTTPS uses TLS peer/hostname verification, no redirects, bounded responses, and timeouts.

## Immutable package indexes

Each package uses:

```text
v1/packages/<package>.index
```

with `EMJREGPKG1`. Every exact SemVer is bound to package content SHA-256 and exact artifact SHA-256. The artifact is addressed at:

```text
v1/artifacts/sha256/<artifact-sha256>.emjpkg
```

`emji versions` reads the index. `emji fetch` performs SemVer selection, retrieves the exact artifact, verifies package/version/content/artifact identity, and only then admits it to the registry-scoped cache. Cache hits are re-verified.

## Publication

File registry publication is immutable and credential-free. Exact republishing is idempotent; conflicting content under an existing package/version is rejected.

HTTPS publication uses the authenticated `emjpub1` contract documented in [AUTHENTICATED_REGISTRY_PUBLICATION.md](AUTHENTICATED_REGISTRY_PUBLICATION.md). Registry identity is verified before the bearer credential is sent. The actual `.emjpkg` bytes are uploaded, and the returned receipt is strictly verified and persisted.

## Remote project dependencies

Registry dependencies are first-class manifest/lock state. `emji add ... --registry` and `emji sync` resolve recursively, verify artifacts, materialize content-addressed packages under the project-owned `.emojineer/packages` store, and write deterministic lock v3 provenance. `emji sync --offline` can reconstruct from complete verified cache/store state. See [REMOTE_DEPENDENCIES.md](REMOTE_DEPENDENCIES.md).

## Train 19 discovery

Discovery is a lookup layer over immutable registry state, not a replacement for artifact verification.

```text
emji search <query> --registry <endpoint> [--include-prerelease] [--json]
emji package-info <package> --registry <endpoint> [--include-prerelease] [--json]
emji dependents <package> --registry <endpoint> [--include-prerelease] [--json]
emji discovery-index --registry <endpoint>
```

File registries derive discovery live from `EMJREGPKG1` indexes and the verified artifacts they reference. Existing file registries therefore require no migration and have no secondary mutable catalog to drift from package truth.

HTTPS registries expose the equivalent bounded canonical resource:

```text
v1/discovery.index
```

using `EMJREGDISC1`. Records bind package/version/content/artifact identities plus entry and direct dependency names. The discovery registry ID must match `EMJREGISTRY1`.

Stable releases are selected by default. `--include-prerelease` allows prereleases. Search terms match package name, entry path, and direct dependency metadata with deterministic AND semantics. Reverse-dependency queries are direct and operate over the selected release of each package.

See [PACKAGE_DISCOVERY.md](PACKAGE_DISCOVERY.md) for the full wire and command contract.

## Trust boundary

Discovery metadata can help a user decide what to fetch, but package code is not trusted merely because it appears in search results. Fetch/materialization still verifies the immutable package index and artifact identities. Discovery cannot make an undeclared transitive dependency importable and cannot grant runtime host authority.
