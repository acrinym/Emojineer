# Package Search and Registry Discovery

Train 19 adds deterministic registry discovery to the real `emji` package workflow. It does not add a fake registry, a second package model, or runtime network authority.

## Product contract

A discovery record is metadata about one immutable registry package version. Every record binds:

- package name;
- exact SemVer;
- package `content-sha256`;
- exact `.emjpkg` `artifact-sha256`;
- package entry source;
- sorted direct dependency names.

For file registries, `emji` derives discovery metadata from the existing `EMJREGPKG1` package indexes and verified immutable artifacts. This makes existing file registries discoverable without migration or a second source of truth.

HTTPS registries expose the same metadata through a bounded canonical index at:

```text
v1/discovery.index
```

with format marker:

```text
EMJREGDISC1
```

The discovery index carries the registry ID and deterministic package-version records. Its registry ID must match the already-verified `EMJREGISTRY1` descriptor. Records are bounded, validated, uniquely keyed by package/version, and canonically ordered.

Discovery metadata is advisory lookup data, not executable authority. A later `fetch`, `add`, `sync`, compile, or run still follows the existing immutable package-index/artifact verification path before package code can be admitted or executed.

## Search

```text
emji search <query> --registry <endpoint>
emji search <query> --registry <endpoint> --include-prerelease
emji search <query> --registry <endpoint> --json
```

`*` lists all eligible packages. Other queries are ASCII-case-insensitive AND queries over canonical discoverable fields:

- package name;
- entry path;
- direct dependency names.

This deliberately avoids adding an unverified publisher-controlled tag channel in Train 19. Richer package-description/tag metadata can extend the immutable manifest model later without changing discovery authority boundaries.

Search returns one selected release per package. By default only stable releases participate. `--include-prerelease` allows prereleases and selects the highest SemVer across stable and prerelease candidates.

## Package metadata

```text
emji package-info <package> --registry <endpoint>
emji package-info <package> --registry <endpoint> --include-prerelease
emji package-info <package> --registry <endpoint> --json
```

Package info reports the selected release, stability, entry, immutable identities, direct dependencies, and all discoverable versions.

## Reverse dependencies

```text
emji dependents <package> --registry <endpoint>
emji dependents <package> --registry <endpoint> --include-prerelease
emji dependents <package> --registry <endpoint> --json
```

Reverse dependency queries are direct and deterministic. They inspect the selected release of each package under the same stable/prerelease policy and return packages whose immutable metadata names the requested package as a direct dependency.

## Canonical discovery index

```text
emji discovery-index --registry <endpoint>
```

For a file registry this materializes the canonical `EMJREGDISC1` view derived from package indexes and artifacts. It can be used by registry operators to produce the static HTTPS discovery resource. For HTTPS it validates and re-renders the served index canonically.

## Deterministic JSON schemas

Tooling can request stable JSON views:

- `emojineer.registry-search.v1`
- `emojineer.registry-package-info.v1`
- `emojineer.registry-dependents.v1`

No absolute checkout paths or timestamps appear in these views.

## Authority and safety boundaries

- Discovery networking exists only in explicit `emji` package-manager commands.
- `emojineer` programs and the VM receive no network/filesystem/process/clock/randomness authority from discovery.
- HTTPS discovery keeps certificate and hostname verification enabled, rejects redirects, and uses bounded responses/timeouts.
- File-registry discovery re-verifies the immutable artifacts named by package indexes rather than trusting filenames.
- Discovery never replaces the verified `fetch`/materialization path.
- Undeclared transitive dependencies do not become ambient imports merely because discovery can see them.

## Train 19 acceptance journey

1. initialize a real file registry;
2. publish stable and prerelease versions of packages;
3. publish packages with direct dependency metadata;
4. search by package name and by dependency keyword;
5. verify stable-only selection and explicit prerelease inclusion;
6. inspect package metadata in human and deterministic JSON form;
7. query reverse dependencies;
8. render and reparse the canonical `EMJREGDISC1` index;
9. run the complete flow through the real `emji` executable;
10. qualify the whole repository in both Debug and Release builds.
