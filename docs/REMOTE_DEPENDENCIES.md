# Remote Dependencies and Reproducible Materialization

Emojineer 0.15 turns the verified registry artifacts introduced in 0.13 and the registry transport introduced in 0.14 into real project dependencies.

The product rule is simple: a registry dependency must behave like a real package dependency after resolution, while ordinary compilation and execution remain offline and receive no ambient network authority.

## Manifest model

Local/path dependencies remain backward compatible:

```toml
[dependencies]
local_math = "../local-math"
```

Registry sources are named separately so dependency declarations remain compact and source provenance is explicit:

```toml
[registries]
public = "https://packages.example.org/emojineer"

[dependencies]
mathkit = "registry:public:^1.4.0"
textkit = "registry:public:~2.1.3"
local_math = "../local-math"
```

A dependency value beginning with `registry:` has the canonical form:

```text
registry:<registry-alias>:<SemVer-requirement>
```

Registry aliases use the normal portable identifier rules. Registry endpoints use the Train 14 endpoint parser and therefore support HTTPS plus file registries for development/testing. Plain HTTP and unknown URL schemes remain rejected.

Canonical manifest output sorts registry aliases and dependencies. Registry declarations and registry dependency requirements are part of package content identity, so a package cannot silently change where or what it depends on without changing its package SHA-256.

The same package name may not be declared as both a path dependency and a registry dependency by one package.

## Real remote add

The existing local workflow remains:

```text
emji add mathkit ../mathkit
```

A remote dependency is explicit:

```text
emji add mathkit '^1.4.0' --registry https://packages.example.org/emojineer
```

The default registry alias is `origin`. A caller may choose another stable alias:

```text
emji add mathkit '^1.4.0' --registry https://packages.example.org/emojineer --registry-name public
```

Remote add is transactional at the manifest/lock level. It writes the candidate declaration, performs the same resolution/materialization path as `emji sync`, and commits the manifest/lock only when the resulting graph is valid. Failed selection, fetch, verification, materialization, package-name mismatch, or source-graph validation must not leave a broken dependency declaration behind. Content-addressed cache/store objects that were safely verified may remain because they are immutable and reusable.

`emji remove` removes either source kind and refreshes the dependency state. Unreferenced content-addressed store objects may remain as cache; they are not part of the resolved graph.

## Sync and materialization

```text
emji sync
emji sync path/to/project
emji sync --offline
emji sync path/to/project --cache path/to/cache
```

Online sync:

1. parses and validates the root manifest;
2. resolves path dependencies normally;
3. resolves every registry dependency against its named registry;
4. selects a deterministic matching version using the existing SemVer implementation;
5. fetches an immutable `.emjpkg` through the Train 14 verified transport/cache path;
6. verifies package name, selected version, package content SHA-256, artifact SHA-256, manifest canonicality, source checksums, and entry presence;
7. parses the artifact's embedded canonical manifest and recursively resolves its direct dependencies;
8. materializes verified package-owned files into the project package store;
9. validates the complete package graph, direct-dependency ownership, package-aware source graphs, and package-name uniqueness;
10. writes deterministic lock format 3 only after the graph is complete.

Registry artifacts are never extracted to caller-selected arbitrary paths.

## Deterministic package store

Materialized packages live below the project-owned store:

```text
.emojineer/packages/<registry-key>/<package>/<version>/<artifact-sha256>/
```

The materialized directory contains exactly the verified canonical `emojineer.toml` plus package-owned `.emoji` sources represented by the artifact. Paths are created only from parser-validated portable relative artifact paths.

Materialization uses a same-parent staging directory and commits a complete verified directory. Existing content-addressed materializations are revalidated before reuse. A corrupt or partial store object is rejected or rebuilt from a verified cached artifact; it is never trusted merely because its directory name contains a SHA-256.

The package store is implementation state, not source-authoritative user code.

## Recursive dependency resolution

Remote packages may themselves contain path or registry dependency declarations.

Registry dependency aliases/endpoints embedded in the package's canonical manifest are honored as package semantics. Path dependencies inside a distributed registry artifact are not magically available from the publisher's checkout. A package published for registry consumption must therefore not rely on unresolved external path dependencies. Publication/sync diagnostics must make that boundary explicit rather than fabricating a path.

A resolved graph continues to enforce one package root/version for one package name. If multiple graph edges constrain one registry package, all requirements must agree on a single selected version. The resolver must either choose one deterministic version satisfying all active requirements or report the conflicting requirement set; it must not silently let traversal order choose different results.

Package cycles remain package-cycle errors and stay distinct from source-module cycles.

## Lock format 3

Projects with registry dependencies use deterministic lock format 3. The root retains manifest drift identity. Every dependency record declares its source kind.

A path record carries at least:

```text
source = "path"
name = "local_math"
version = "0.4.0"
path = "../local-math"
content_sha256 = "..."
dependencies = "..."
```

A registry record carries at least:

```text
source = "registry"
name = "mathkit"
registry = "public"
registry_id = "emojineer.public"
registry_endpoint = "https://packages.example.org/emojineer"
requirements = "^1.4.0"
version = "1.7.2"
content_sha256 = "..."
artifact_sha256 = "..."
store_path = ".emojineer/packages/..."
dependencies = "..."
```

If multiple parents constrain a package, `requirements` is a deterministic sorted representation of the active requirement set.

Records are sorted, timestamp-free, and use project-relative paths for project-owned materialization. The lock must never substitute mutable registry state for the selected immutable identities.

Changing a registry alias/endpoint, dependency requirement, selected version, content identity, artifact identity, dependency edge, materialized package content, or root manifest makes the dependency state stale.

## Offline reproducibility

Ordinary `emojineer run/check/compile`, module linking, and `emji check/tree` do not contact registries.

They resolve registry packages from lock format 3 plus the verified materialized store. Missing/stale/corrupt remote state fails with a package-manager diagnostic instructing the user to sync; it does not silently use the network.

`emji sync --offline` performs no registry request. Given a complete valid lock and verified content-addressed cache/store, it can reconstruct missing materialized package directories and validate the complete graph. If an immutable artifact required by the lock is unavailable or corrupt, offline sync fails explicitly.

## Package-aware imports

Once synced, registry packages participate in the existing `PackageGraph` exactly where package semantics matter:

```emoji
🔗 📜pkg:mathkit/src/main.emoji📜
```

Direct-dependency enforcement remains unchanged. A transitive registry package does not become an ambient import. Package-root ownership checks, nested-package boundary checks, export visibility, duplicate symbol behavior, and source-module cycle diagnostics apply to path and registry packages alike.

Graph inspection should expose dependency source/provenance without leaking machine-specific absolute materialization roots. Human and JSON tree output should distinguish path vs registry packages and, for registry packages, expose selected version plus immutable identities.

## Safety and authority boundary

Registry networking belongs to explicit `emji` package-manager commands only.

No registry operation grants Emojineer language programs ambient network, filesystem, process, shell, clock, randomness, credential, or host-resource authority.

Credentials are not stored in `emojineer.toml`, package artifacts, or lockfiles. Authenticated HTTPS publication remains Train 16.

## Train 15 acceptance journey

A real acceptance journey is:

1. create and publish multiple immutable versions of a library to a local registry;
2. create and publish a second library that depends on the first through registry metadata;
3. initialize an application;
4. run `emji add <library> <requirement> --registry <registry>`;
5. observe recursive resolution, verified fetch, deterministic materialization, and lock v3;
6. import the direct remote package with `pkg:` and compile/run through the normal linker/VM;
7. remove the registry directory or disable network access;
8. run/check successfully from the locked materialized graph;
9. delete one materialized package and use `emji sync --offline` to reconstruct it from the verified cache;
10. corrupt a store/cache object and observe verification reject it rather than executing it;
11. change a requirement and observe stale-state detection until the project is synced again.

That journey, not merely parser/unit-level coverage, defines Train 15 completion.
