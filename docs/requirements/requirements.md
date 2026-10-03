# Requirements (as implemented)

Scope: an educational wallet/payment simulator, single process, single currency in practice, CLI front-end, JSON persistence. Roles: Customer, Merchant, Admin.

## Functional requirements
| # | Group | Requirement | Validation / failures | Where |
|---|---|---|---|---|
| F1 | Users | Register customer / merchant; admins created by bootstrap or another admin | name 1–80, valid email, optional 7–15 digit phone, password ≥ 8 with letters+digits; duplicate email (case-insensitive) → `DuplicateUser` | `AuthService` |
| F2 | Auth | Login/logout, lockout after N failures, admin unlock/suspend | wrong credentials → generic `InvalidCredentials`; locked → `AccountLocked`; suspended → `AccountInactive` | `AuthService`, `AdminService` |
| F3 | Wallet | One wallet per customer/merchant; credit, debit, freeze, unfreeze, close | zero amount, insufficient funds, frozen, closed, currency mismatch | `Wallet` |
| F4 | Money | Add money (bank, debit, credit, cash), withdraw | method validation (number formats, cash cap), insufficient funds | `PaymentService`, `PaymentMethod` |
| F5 | Payments | Transfer, merchant payment, QR payment, bill payment | unknown payee/merchant, self-payment, inactive/frozen/closed counterpart | `PaymentService` |
| F6 | Merchants | Payment id, business name/category, QR identifier generate/parse, refund handling | invalid identifier → `InvalidIdentifier` | `Merchant`, `PaymentIdentifier` |
| F7 | Transactions | Record every attempt with status, reason, links; search/filter | illegal transition → `InvalidTransition` | `Transaction`, repositories, `QueryService` |
| F8 | Refunds | Full/partial, linked, capped, idempotent, customer request + merchant decision | over-refund, duplicate request, wrong merchant, non-refundable type | `RefundService` |
| F9 | Notifications | Notify on payments, failures, reviews, refunds, admin actions via several channels | missing phone → SMS skipped | `notifications/`, `Notifier` |
| F10 | Risk | Large amount, velocity, repeated failures → approve/review/reject | thresholds from config | `FraudEngine` |
| F11 | Limits | Per-transaction, daily amount, daily count per tier; admin configurable | invalid configuration rejected | `LimitPolicy`, `AdminService` |
| F12 | Audit | Record sensitive operations (incl. denied/failed/suspicious) | — | `AuditLogger` |
| F13 | Admin | List/view users, freeze/unfreeze, status, review queue, audit, limits, reports | all behind `Permission` checks | `AdminService` |
| F14 | Persistence | Survive restarts; detect corruption | corrupt/invalid file → start-up error, file untouched | `JsonDatabase` |
| F15 | Reporting | Summary, top merchants, highest, recent, per-day, busiest users, suspicious, CSV | admin only | `ReportService` |
| F16 | Configuration | Externalised limits/thresholds/lockout/hash iterations | missing file/keys → defaults | `AppConfig` |
| F17 | Idempotency | Same key + same payload = original result; different payload = error | `IdempotencyMismatch` | `PaymentService`, `RefundService` |

## Non-functional requirements
* **Maintainability / extensibility**: new rule, channel, payment method or storage backend without editing the payment pipeline (interfaces + composition root).
* **Reliability / consistency**: integer money, double-entry ledger, all-or-nothing settlement, reconciliation checks, atomic file replace.
* **Testability**: constructor injection, injectable clock, in-memory database; 83 automated tests (see `docs/testing/testing.md`).
* **Performance**: O(1) lookups, O(log n) range queries (documented in `docs/design/dsa-and-complexity.md`); measured performance **Not measured yet**.
* **Error handling**: `Result` for business failures, exceptions only for invariant violations.
* **Logging**: levelled application log (`data/app.log`) without secrets; separate audit trail.
* **Security (simulated)**: hashing, lockout, RBAC, validation — educational, not production-grade (see `docs/design/security.md`).
* **Usability**: role-specific menus, clear messages with error codes, malformed input never crashes the CLI.
