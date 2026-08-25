# Authenticated Registry Publication

Train 16 turns remote publication into a real authenticated write protocol. It preserves the immutable package/artifact identities and verified read path shipped in Trains 13–15.

## `emjpub1` wire contract

Remote publication is HTTPS-only and versioned as `emjpub1`. Plain HTTP, redirects, URL userinfo/embedded credentials, and cross-origin credential forwarding are rejected.

Before sending any credential, the client performs an unauthenticated `GET <registry-base>/v1/identity` with `Accept: application/vnd.emojineer.registry.identity.v1+json`. A successful `200` response must contain the canonical registry id. The client validates and remembers that id before any credentialed write. Identity failure, malformed identity, redirects, or an oversized identity response abort publication before the bearer credential is sent.

Publication uses `POST <registry-base>/v1/publish` with `Authorization: Bearer <token>`. The request is a bounded `multipart/form-data` body with exactly two semantic parts: metadata using `application/vnd.emojineer.publication.metadata.v1+json`, and the exact `.emjpkg` bytes using `application/vnd.emojineer.package.v1+octet-stream`. The metadata binds namespace, package name, exact semantic version, package-content SHA-256, and artifact SHA-256. The client requests `application/vnd.emojineer.publication.receipt.v1+json` and verifies the returned receipt before reporting success.

Successful responses are 2xx and must return a valid `emjpub1` receipt. `400` denotes malformed/invalid publication data, `401` missing or invalid authentication, `403` namespace/package authorization failure, `409` immutable same-version conflict, and oversized request/response or transport-deadline failures are reported as bounded transport failures. Other non-2xx responses are protocol failures. Server response text is untrusted and must never cause credentials or reflected secrets to appear in diagnostics.

## Credentials

Credentials are external authority. Emojineer MUST NOT accept a raw secret as a normal command-line argument. The current CLI reads the bearer token indirectly from `EMOJINEER_TOKEN`; future credential-store, named-environment-reference, stdin, or file-descriptor integrations may provide equivalent indirect inputs.

The token must never be serialized into `emojineer.toml`, package artifacts, `emojineer.lock`, source files, publication receipts, help output, normal diagnostics, or logs. Credential values are validated before header construction and must reject CR, LF, NUL, and other control/header-injection characters. Emojineer does not intentionally propagate the token to child processes.

## Immutable publication semantics

Registry namespace/package ownership is authoritative. Exact republishing of identical immutable content MUST succeed idempotently. Equality is defined by registry id, package name, exact version, package-content SHA-256, and artifact SHA-256. Publishing different immutable content under an existing package/version MUST fail with a conflict and must not replace the committed artifact.

The server receipt must bind the verified registry id, package name, exact version, package-content SHA-256, artifact SHA-256, protocol version, and an opaque receipt id. Any mismatch or malformed/tampered receipt is rejected before success is reported.

## Durable receipts

Every successful authenticated publication MUST persist a receipt. Unless `--receipt <file>` is supplied, `emji publish` writes `<package-root>/.emojineer-receipt.json`.

Receipt serialization is UTF-8 JSON with one object and a canonical stable key order containing: `artifact_sha256`, `content_sha256`, `package_name`, `protocol_version`, `receipt_id`, `registry_id`, `timestamp`, and `version`. String values are JSON-escaped correctly. Required identity fields must be non-empty and SHA fields must be valid lowercase SHA-256 before persistence. The receipt contains no credential. Receipt files are committed atomically using the same safe same-directory atomic-write machinery as other Emojineer durable metadata.

## Transport bounds and deadlines

`emjpub1` enforces concrete bounds. The total authenticated publication request is capped at 128 MiB, including the artifact and protocol framing. The publication receipt response is capped at 16 KiB. Registry identity responses are capped at 4 KiB. Connection establishment has a 10-second deadline, response headers have a 30-second deadline, and the complete publication operation has a 300-second deadline. Exceeding any bound or deadline fails publication without reporting success. Implementations must not silently fall back to an unbounded or unauthenticated path.

TLS peer and host verification are mandatory. Redirect following remains disabled for both identity and credentialed publication requests.

## CLI dispatch

`emji publish` selects immutable local publication for file registries and authenticated `emjpub1` publication for HTTPS registries. File/local registries continue to work without credentials. Ordinary Emojineer programs and the VM receive no ambient network authority from this feature.

## Acceptance journey

1. Build and pack a package.
2. Verify registry identity without sending a credential.
3. Publish the exact artifact bytes with a valid namespace credential.
4. Verify and atomically persist the server receipt.
5. Fetch the same artifact through the existing verified read path.
6. Republish identical content successfully and idempotently.
7. Reject conflicting same-version content without replacing the committed artifact.
8. Reject missing, invalid, header-injecting, or wrong-namespace credentials.
9. Prove diagnostics and receipts do not disclose or reflect the credential.
10. Prove request/response bounds, deadlines, and tampered identity/receipt/hash failures.
11. Prove local file-registry publication still works unchanged.

The interoperability fixture must exercise the actual protocol framing and exact artifact bytes rather than bypassing the transport through helper-only success paths.

## Boundaries

- No credentials in manifests, artifacts, locks, source, receipts, help text, or logs.
- No raw secret command-line argument.
- No generic unauthenticated remote write.
- No redirect-based credential forwarding.
- No weakened TLS.
- No ambient network authority for Emojineer programs or the VM.
- No audit machinery.
