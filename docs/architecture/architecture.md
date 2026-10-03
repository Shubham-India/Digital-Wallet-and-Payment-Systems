# Architecture

## Layers and dependency rule

```
Presentation  →  Services  →  Domain  ←  (interfaces) Repositories / Infrastructure implementations
                     ↘ Policies, Notifications (abstractions + implementations)
App (AppContext) is the composition root and may see everything.
```

Dependencies point **inward**: `domain/` includes nothing from other layers (only `utils/TimeUtil.h` and the standard library). Services depend on repository *interfaces* declared in `repositories/Repositories.h`; the concrete `InMemory*` / `Json*` classes are chosen only in `AppContext`.

| Layer (folder) | Contents | Rule |
|---|---|---|
| `domain/` | `Money`, `Wallet`, `User` family, `Transaction`, `LedgerEntry`/`RefundRequest`/`AuditEvent` (in `Records.h`), `PaymentMethod` family, `PaymentIdentifier`, `Result`/`Error` | Pure business objects. No I/O, no JSON, no printing. |
| `policies/` | `FraudRule`, `FraudEngine`, `LimitPolicy`, `TierLimitPolicy`, `LimitPolicyFactory` | Replaceable business rules (Strategy). |
| `notifications/` | `NotificationService` + Console/Email/SMS/Push/InApp, `EventPublisher` | Output channels (Observer). |
| `services/` | `AuthService`, `PaymentService`, `RefundService`, `SettlementService`, `AdminService`, `QueryService`, `ReportService`, `AccessControl`/`SessionManager`, `AuditLogger`, `Notifier`, `FraudReviewQueue` | Use cases. All authorization and orchestration. The CLI never decides anything. |
| `repositories/` | `Repositories.h` interfaces, `InMemory*`, `JsonDatabase` | Persistence behind interfaces. JSON mapping lives only here. |
| `infrastructure/` | `Config`, `Json`, `Clock`, `Logger`, `PasswordHasher`, `IdGenerator` | Technical utilities. |
| `presentation/` | `Console`, `Menu`, `Formatter`, controllers, `Demo` | Translate input/output only. |
| `app/` | `AppContext` | Manual dependency injection; no globals or singletons. |

## Request, transaction, ledger entry, balance

These four are deliberately separate:

| Concept | Class | Meaning | Mutability |
|---|---|---|---|
| Payment request | `PaymentCommand` | What the caller asks for (+ idempotency key) | transient |
| Transaction | `Transaction` | Business record of one attempt: type, parties, status, failure reason, links | status changes through a guarded state machine |
| Ledger entry | `LedgerEntry` | Accounting posting (one debit + one credit per settled movement) | append-only |
| Wallet balance | `Wallet::balance_` | Current derived state | only via `credit`/`debit` |

Consistency check available at any time: for every wallet, Σcredits − Σdebits over its ledger entries equals its balance (`SettlementService::reconciles`); for every settled transaction debits = credits (`balanced`). The tests and the demo verify both.

## Payment pipeline (`PaymentService::execute`)

1. **Authorize** the session for the permission the type needs (`AccessControl`).
2. **Validate** amount > 0 and idempotency key present.
3. **Idempotency check** on `userId:key`: same key + same fingerprint → return the original outcome (`duplicate = true`); same key + different payload → `IdempotencyMismatch`.
4. **Create** the `Transaction` (PENDING), **resolve routing** (payee user/wallet, or the external side), then save it so even failed attempts are recorded.
5. **Eligibility**: payer active, wallet exists; top-ups ask the `PaymentMethod` to authorize.
6. **Limits** (`LimitPolicy` for the payer's tier, using today's settled outgoing payments).
7. **Feasibility probe** on a copy of the payer wallet (frozen/closed/insufficient fail here precisely).
8. **Risk assessment** (`FraudEngine`): `REJECT` → FAILED + audit `SUSPICIOUS`; `REVIEW` → `PENDING_REVIEW`, pushed to the review queue, no money moves; `APPROVE` → continue.
9. **Settle** (`SettlementService`): both wallets are changed on copies; only if both succeed are the wallets saved and the two ledger entries appended. Transaction → PROCESSING → SUCCESS.
10. **Audit** event, **notifications** queued, **commit** to the database, notifications dispatched.

### Failure behaviour
Every failure after step 4 goes through one function (`PaymentService::abort`): the transaction becomes FAILED with a code and reason, an audit event and a notification are written, and **no wallet or ledger change has happened** (steps 1–8 never mutate balances; step 9 is all-or-nothing). Failures before step 4 (not authorized, bad amount, missing key) create no transaction but are audited.

### Atomicity scope
Atomic *at application level*: in-memory repositories cannot fail halfway through `settle`, and the JSON backend writes each changed file with write-temp-then-rename in a single `commit()` at the end of the operation. A crash between two file renames could leave files from different moments; a real system would use a database transaction (the `Database::commit()` seam is where SQLite would plug in). If `commit()` itself fails the caller gets `PersistenceFailure`.

## Refunds
`RefundService` creates a new transaction of type `REFUND` (merchant wallet → customer wallet) with `linkedTxId` = original. The original's `refundedAmount` can never exceed its amount (`Transaction::applyRefund`), which blocks over-refunds and duplicates; an idempotency key (scoped per actor) makes a retried request return the earlier refund. Customers *request* refunds (`RefundRequest`); the merchant approves (→ `issueRefund`) or rejects.

## Persistence
`JsonDatabase` extends `InMemoryDatabase`: repositories operate on memory; `load()` fills them from `users.json`, `wallets.json`, `transactions.json`, `ledger.json`, `refunds.json`, `audit.json`; `commit()` rewrites only files whose repository is dirty. A corrupt/invalid file makes start-up fail with a clear message and the file is left untouched. On start the `IdGenerator` is re-seeded from stored ids and the fraud review queue is rebuilt from `PENDING_REVIEW` transactions.

## Authorization
`AccessControl::roleHas(role, permission)` is the single role→permission table; `AccessControl::authorize(session, permission)` additionally checks the session is live (`SessionManager`). Services call it first. Non-admin transaction queries are forced to the caller's own user id in `QueryService`.

## Configuration
`config/app.json` → `AppConfig` (limits per tier, fraud thresholds, lockout attempts, PBKDF2 iterations, log level). Missing file → compiled defaults; missing keys → defaults.
