# Emojineer 1.x Compatibility Policy

This document freezes the compatibility contract that begins with Emojineer 1.0.
It distinguishes source compatibility, serialized formats, package state, and
host/tooling protocols so that a compatible change in one layer is not mistaken
for permission to silently change another.

## Source language

Within 1.x, documented `.emoji` syntax and documented runtime semantics must not
silently change meaning. Existing valid source may gain clearer diagnostics, but
a previously documented construct may not be repurposed for a different meaning
without explicit versioning and migration guidance.

Unicode token canonicalization and grapheme handling are language semantics.
Variation-selector equivalence already documented by the language remains part
of the 1.x contract.

`.emjweb` is a separately versioned native authoring surface. Its typed
`emojineer.web-ir.v1` lowering is part of the 1.x product contract; web markup
must not become hidden HTML/JavaScript source.

## EMJBC

The 1.0 writer emits EMJBC v10. The 1.x reader supports EMJBC v1 through v10.
Those historical layouts are regression-tested by release-hardening fixtures.
Readers must reject unsupported versions and malformed/truncated input rather
than reinterpret it.

A future serialized change that cannot be represented by v10 requires a new
EMJBC version. Adding source-only syntax, linker behavior, documentation, or
separately represented tooling does not by itself justify changing EMJBC.

See [BYTECODE.md](BYTECODE.md) for the exact version history and verifier rules.

## Package artifacts, manifests, and locks

`.emjpkg` uses the immutable `EMJPKG1` framing introduced by the package-artifact
work. The 1.0 reader continues to exercise that v1 framing independently of the
current writer. Package content identity and artifact identity remain SHA-256
contracts and may not be silently redefined inside 1.x.

The current project lock writer emits lock format 3. Lock format 3 is the 1.x
lock contract for path and registry dependencies, including registry identity,
selected version, immutable hashes, materialized store location, and dependency
edges. Pre-format-3 development locks should be regenerated with `emji lock` or
`emji sync`; they are not a 1.x portability promise.

Manifest keys and lock fields may only be extended compatibly when old readers
either ignore the extension safely by documented rule or fail explicitly.

## Capabilities and host authority

Capability names and their meaning are compatibility-sensitive. A 1.x release
must not silently broaden an existing grant. Default execution remains
zero-authority; sandbox remains hard zero-host-authority. New host facilities
must remain verifier-visible and participate in whole-program preflight.

## Interop and EASM

`EMJABI1` signatures, adapter envelopes, and documented external-name rules are
1.x contracts. EASM remains the separate verified `EASM1` low-level text
representation. Neither may gain an implicit ambient host escape.

## Registry protocols

`EMJREGISTRY1`, `EMJREGPKG1`, discovery records, `emjpub1`, immutable artifact
hashes, and publication receipts are protocol contracts. Changes that would
make an existing valid record mean something different require a new protocol
or format identifier.

## Diagnostics and implementation freedom

Diagnostic wording may improve unless a test or documented machine-readable
interface explicitly freezes it. Internal C++ structure, algorithms, cache
layout not documented as public, and performance characteristics may change as
long as the observable contracts above remain intact.

## Breaking changes

A deliberate incompatibility requires all of the following:

1. an explicit format/protocol/language version boundary;
2. release notes naming the incompatibility;
3. migration or regeneration instructions;
4. regression coverage for the previous supported form where a reader remains promised;
5. no silent reinterpretation of old serialized bytes or source.
