# Security considerations

> **Educational simulation; not production-grade financial security.** No compliance with PCI-DSS, RBI directions or any banking standard is claimed.

| Topic | What this project does | What it does NOT do |
|---|---|---|
| Password storage | Per-user random 16-byte salt + PBKDF2-HMAC-SHA256 (configurable iterations, default 20 000); constant-time comparison. SHA-256/HMAC/PBKDF2 are implemented from FIPS 180-4 / RFC 8018 and verified against published vectors in `tests/test_core.cpp`. | Not a vetted library; default iteration count is far below current production guidance; no Argon2/bcrypt/scrypt; no pepper/HSM. |
| Authentication | Email + password, session token object, lockout after `maxFailedLogins` (admin unlock), same error for unknown email and wrong password, login events audited. | No MFA/OTP, no CAPTCHA, no IP rate limiting, no session expiry, tokens not bound to a transport. |
| Authorization | Central `AccessControl` permission matrix, checked at every service entry; non-admins can only query their own transactions; refund endpoints verify the actor owns the sale; admins cannot lock themselves out. | No fine-grained attribute policies, no separation of duties between admins. |
| Input validation | `Money::parse` strictness, email/phone/password checks, QR identifier whitelist, enum parsing, JSON parser limits (depth 64, integer-only) and record validation on load. | No Unicode normalisation or length limits on every free-text field beyond key ones. |
| Idempotency | Per-user keys; fingerprint compare; replay returns the original outcome; failed requests are also replayed as failed (client must use a new key to retry). Records are kept permanently (they are the transaction records). | No expiry/retention policy; a real system would expire keys after a defined window. |
| Audit logging | Append-only `AuditEvent`s for logins, payments, refunds, admin actions, denied and suspicious operations. Metadata never contains passwords or hashes (tested). | Not tamper-evident (no hash chain/signing); stored in plain JSON. |
| Sensitive data | Card/account numbers are masked in `describe()`; only the masked form is stored in transactions; logs contain ids and outcomes only. | `data/*.json` is unencrypted; password hashes and personal data are readable by anyone with file access. |
| Transaction integrity | Integer money, double-entry ledger, atomic settlement on copies, legal-transition state machine, refund caps, reconciliation checks. | No cross-process locking; no crash-proof multi-file transaction. |
| Replay / double payment | Idempotency keys (client retries) and state machine (a transaction cannot be settled twice: `SUCCESS` cannot go back to `PROCESSING`). | No nonce/timestamp request signing. |
| Default credentials | A demo admin is created on first start (`admin@wallet.local`). | Must be changed/removed outside classroom use. |

Business-transaction log vs audit log: the transaction table/ledger records *money movements* (including failed attempts); the audit log records *who did what*, including actions that move no money (logins, freezes, denials).
