# Authenticated Registry Publication

Train 16 turns remote publication into a real authenticated write protocol while preserving the immutable package/artifact identities and verified read path shipped in Trains 13–15.

## `emjpub1` wire contract

Remote publication uses protocol `emjpub1` over HTTPS only.

### 1. Registry identity preflight

Before any credential-bearing request is constructed or sent, the client MUST verify the selected registry through the already-shipped Train 14 discovery exchange:

```text
GET <registry-endpoint>/v1/registry.txt
Accept: text/plain
```

The response is the bounded `EMJREGISTRY1` descriptor parsed by `registry_identity()`. Redirects are not followed. A failed, malformed, or mismatched identity preflight aborts publication before the bearer credential is sent anywhere.

### 2. Publication request

After identity verification succeeds, the client sends exactly one authenticated write request containing the actual immutable `.emjpkg` bytes:

```text
POST <registry-endpoint>/v1/publish
Authorization: Bearer <credential>
Content-Type: application/vnd.emojineer.publish.v1+octet-stream
Accept: application/vnd.emojineer.publish-receipt.v1+json
X-Emojineer-Protocol: emjpub1
X-Emojineer-Namespace: <namespace>
X-Emojineer-Package: <package>
X-Emojineer-Version: <exact-semver>
X-Emojineer-Content-SHA256: <package-content-sha256>
X-Emojineer-Artifact-SHA256: <whole-artifact-sha256>

<body: exact .emjpkg bytes>
```

The registry MUST parse and verify the received artifact and reject any mismatch between its body and the package, version, content SHA-256, or artifact SHA-256 named by the request.

### 3. Authentication and authorization

The raw credential is never accepted as a command-line argument. The current client reads the secret only from `EMOJINEER_TOKEN`; a future credential-store or file-descriptor integration may provide another indirect source without placing secret material in `argv`.

`--namespace` is non-secret publication metadata and remains an ordinary CLI option.

Credential material MUST NOT appear in:

- `argv` or help examples;
- `emojineer.toml`;
- `emojineer.lock`;
- `.emjpkg` artifacts;
- source files;
- publication receipts;
- normal diagnostics or server-response echoes;
- child-process arguments or environment created by the publisher.

Credential values containing whitespace/control characters that could inject HTTP headers are rejected before transport.

Status mapping is deterministic:

- `400` — malformed publication or immutable identity/checksum mismatch;
- `401` — missing/invalid credential;
- `403` — credential lacks authority for the namespace/package;
- `409` — exact package/version already exists with different immutable content;
- `413` — request exceeds registry publication bound;
- any other non-2xx — generic HTTPS publication failure containing only the status code, never the response body.

### 4. Immutable republishing

Exact republishing of identical immutable content **MUST succeed idempotently**.

Identity equality is the tuple:

1. verified registry id;
2. package name;
3. exact semantic version;
4. package content SHA-256;
5. whole-artifact SHA-256.

A second publication of that same tuple must succeed without replacing the immutable artifact. A differing content/artifact identity under the same registry/package/version MUST fail with `409` and MUST NOT mutate the existing version.

### 5. Success receipt

A successful response uses:

```text
Content-Type: application/vnd.emojineer.publish-receipt.v1+json
```

The body is one JSON object containing **exactly** these string fields:

- `artifact_sha256`
- `content_sha256`
- `package_name`
- `protocol_version`
- `receipt_id`
- `registry_id`
- `timestamp`
- `version`

The canonical serialized field order is exactly the order above, with normal JSON string escaping and no additional fields. Duplicate fields, unknown fields, malformed JSON, trailing data, oversized receipts, unsupported protocol versions, malformed identities, or mismatches against the verified/uploaded tuple are failures.

The client MUST verify the receipt before reporting publication success.

### 6. Durable receipt persistence

Every successful authenticated publication MUST persist the verified canonical receipt atomically.

Default destination:

```text
<package-root>/.emojineer-receipt.json
```

`--receipt <file>` overrides that destination; it does not make persistence optional.

Persistence uses a same-directory exclusive temporary file, flushes the bytes to durable storage, and atomically replaces the destination (`MoveFileEx(...MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows; `fsync` + POSIX rename on POSIX systems). The receipt contains no credential material.

## Concrete transport bounds

The client enforces these `emjpub1` limits:

| Bound | Value | Failure |
| --- | ---: | --- |
| maximum `.emjpkg` upload body | 134,217,728 bytes (128 MiB) | publication is rejected before network write |
| maximum receipt response body | 16,384 bytes (16 KiB) | response is aborted and rejected |
| TLS/connect deadline | 10 seconds | HTTPS transport failure |
| upload deadline | 300 seconds | `publication upload exceeded 300-second deadline` |
| wait for first response header after upload | 30 seconds | `publication response headers exceeded 30-second deadline` |
| response-body deadline after first header | 300 seconds | `publication response body exceeded 300-second deadline` |

TLS peer verification and hostname verification remain enabled. Redirect following is disabled, so bearer credentials cannot be forwarded cross-origin. Plain HTTP and credential-bearing URL syntax are rejected by endpoint/credential validation.

## Local registries

File/local registry publication remains the Train 14 immutable publication path. It does not require credentials and retains required identical-republish idempotency and same-version conflict rejection.

`emji publish` dispatches by endpoint kind:

- file registry → local immutable publication with no credentials;
- HTTPS registry → Train 14 identity preflight + authenticated `emjpub1` publication + required durable receipt.

## Acceptance journey

The dedicated authenticated-publication CTest interoperability fixture consumes the exact encoded `emjpub1` request produced by the client and enforces the server side of the contract. It proves:

1. registry identity is checked before an authorization-bearing exchange can run;
2. the request contains the actual parseable `.emjpkg` bytes and exact immutable identities;
3. valid authentication and namespace ownership succeed;
4. the uploaded artifact round-trips through the existing verified registry fetch path with identical content/artifact SHA-256 values;
5. identical republish succeeds idempotently;
6. conflicting same-version content is rejected;
7. bad credentials and wrong namespace ownership are rejected;
8. checksum/header mismatch is rejected;
9. request and response byte bounds are enforced;
10. tampered receipts and wrong response media types are rejected;
11. server-controlled failure bodies cannot reflect credential text into diagnostics;
12. receipt files contain only deterministic verified receipt data;
13. local file-registry publication remains unchanged.

## Boundaries

- no raw secret in CLI arguments;
- no credential persistence in project/package state;
- no generic unauthenticated remote write;
- no redirect-based credential forwarding;
- no weakened TLS;
- no metadata-only fake upload path;
- no ambient network authority for Emojineer programs or the VM;
- no audit machinery.
