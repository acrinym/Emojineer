# Authenticated Registry Publication

Train 16 turns remote publication into a real authenticated write protocol. It must preserve the immutable package/artifact identities and verified read path already shipped in Trains 13–15.

## Product contract

Remote publication uses a versioned HTTPS protocol. The client first verifies registry identity, then submits an immutable artifact with explicit authentication and receives a verifiable publication receipt. Publication must never degrade into a generic unauthenticated PUT/POST.

Credentials are external authority. They may come from an explicit CLI option, environment variable, or future credential-store integration, but they must never be serialized into `emojineer.toml`, package artifacts, `emojineer.lock`, source files, publication receipts, or normal diagnostics.

A successful publication response must bind registry id, package name, exact version, package content SHA-256, artifact SHA-256, protocol version, and an opaque server receipt id. The client rejects mismatched or tampered receipt identity before reporting success.

Registries enforce namespace/package ownership and immutable version semantics. Exact republishing of identical immutable content may be idempotent. Publishing different immutable content under an existing package/version must fail.

HTTPS publication requires TLS peer/host verification, bounded uploads/responses, timeouts, no redirects, no credential-in-URL form, no cross-origin credential forwarding, and redacted diagnostics. Plain HTTP is rejected. File/local registry publication remains supported without credentials.

`emji publish` selects local immutable publication for file endpoints and authenticated publication for HTTPS endpoints. Optional receipt output must be deterministic and machine-readable.

## Acceptance journey

1. Build and pack a package.
2. Publish it to a registry endpoint with a valid namespace credential.
3. Verify the server receipt against exact package/version/content/artifact identity.
4. Fetch the same artifact through the existing verified read path.
5. Republish identical content successfully/idempotently.
6. Reject conflicting same-version content.
7. Reject missing, invalid, or wrong-namespace credentials.
8. Prove diagnostics and receipts do not disclose the credential.
9. Prove local file-registry publication still works unchanged.
10. Exercise bounded request/response and tampered-receipt rejection.

## Boundaries

- No credentials in manifests, artifacts, locks, source, receipts, or logs.
- No generic unauthenticated remote write.
- No redirect-based credential forwarding.
- No weakened TLS.
- No ambient network authority for Emojineer programs or the VM.
- No audit machinery.
