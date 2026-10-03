# Digital Wallet & Payment System

A modular **C++20 digital wallet and payment-processing simulator** built to demonstrate object-oriented design, transaction-ledger principles, design patterns, data structures, persistence, validation, audit logging and automated testing.

> **Educational simulation — not a real payment system.** No real money, banks, cards or gateways are involved. Authentication, hashing, "fraud detection" and notifications are simulations meant for learning. The project makes **no** claim of PCI-DSS, RBI or any other regulatory/production-grade security compliance.

## Features

| Area | What is implemented |
|---|---|
| Users | Customer (Basic/Premium tier), Merchant, Admin; registration, login/logout, salted PBKDF2-HMAC-SHA256 password hashes, account lockout after repeated failures, suspend/unlock by admin |
| Wallet | Balance in integer paise (`Money`), credit/debit, freeze/unfreeze/close, encapsulated state |
| Money in/out | Add money from bank account, debit card, credit card or cash deposit (`PaymentMethod` polymorphism); withdraw to bank |
| Payments | Wallet-to-wallet transfer, merchant payment, QR/payment-identifier payment (`NETRAPAY://merchant/<id>/order/<order>?amount=<paise>`), bill payment |
| Transactions | Full lifecycle (`PENDING → PENDING_REVIEW → PROCESSING → SUCCESS/FAILED/CANCELLED → PARTIALLY_REFUNDED → REFUNDED`) with enforced legal transitions |
| Ledger | Double-entry postings for every settled movement, balance reconciliation checks, atomic settlement (no half-applied transfers) |
| Idempotency | Per-user idempotency keys: a retried request returns the original result instead of charging twice; key reuse with a different payload is rejected |
| Risk engine | Configurable rules (`LargeAmountRule`, `VelocityRule` sliding window, `RepeatedFailureRule`) → `APPROVE` / `REVIEW` / `REJECT`; held payments go to a priority review queue |
| Limits | Per-transaction, daily amount and daily count limits per tier (Basic / Premium / Merchant), admin-configurable |
| Refunds | Full & partial refunds, linked to the original transaction, over-refund and duplicate protection, customer refund *requests* approved/rejected by the merchant |
| Notifications | `NotificationService` abstraction with Console, Email, SMS, Push (simulated) and in-app inbox channels, delivered through an Observer-style publisher with a queue |
| RBAC | Central `AccessControl` role → permission matrix, enforced in the service layer |
| Audit | Append-only audit trail (who / what / target / result / metadata) for logins, payments, refunds, admin actions, denied and suspicious operations |
| Reports | System summary, top merchants, highest transactions, per-day counts, busiest users, suspicious transactions, text report and CSV export |
| Persistence | JSON files behind repository interfaces (in-memory implementation also provided); atomic write-then-rename, corruption detected and never overwritten |
| CLI | Role-specific menus (customer, merchant, admin), data-driven `Menu`, robust input validation |

## Build, test, run

Requirements: a C++20 compiler (developed with g++ 16.1 / MSYS2 UCRT64) and GNU make. **CMake is not required.**

```bash
# build (Git Bash / MSYS2 / Linux / macOS; on MSYS2 use mingw32-make)
mingw32-make            # or: make
mingw32-make debug      # unoptimised build with -g
mingw32-make test       # builds and runs the automated tests
mingw32-make run        # start the interactive CLI
mingw32-make demo       # scripted end-to-end demo
mingw32-make clean
```

PowerShell without a POSIX shell: `powershell -ExecutionPolicy Bypass -File scripts/build.ps1` (build + tests), then `build\wallet.exe`.

```bash
./build/wallet.exe --demo                 # scripted walkthrough (fresh data/demo each time)
./build/wallet.exe                        # interactive CLI, data in ./data
./build/wallet.exe --data mydata --config config/app.json
./build/wallet.exe < scripts/sample_session.txt   # piped, non-interactive session
```

First start creates a **demo admin** `admin@wallet.local` / `Admin@1234` (printed by `--help`). It exists so the admin console can be demonstrated — change it (admin menu → create another admin) for anything beyond a classroom demo.

## Example workflow (excerpt of `wallet --demo`)

```
[5] Alice pays the cafe INR 2,500 (limits and risk engine run first)
  [notification] Alice: Payment sent - INR 2500.00 paid (TX-000003).
  [notification] Carol: Payment received - INR 2500.00 received (TX-000003).
    ok: merchant payment succeeded; risk decision APPROVE
    ledger (double entry):
  LED-000005  DEBIT   WAL-0001       INR 2500.00   balance after INR 16500.00
  LED-000006  CREDIT  WAL-0003       INR 2500.00   balance after INR 2500.00

[6] Network retry: the same payment request (same idempotency key) is submitted again
    ok: replayed original TX-000003; Alice was NOT charged twice
...
[15] Integrity checks (ledger vs balances, double entry, money conservation)
    ok: every wallet balance equals the sum of its ledger entries
    ok: every settled transaction has equal debits and credits
```

The demo covers: admin start → register Alice/Bob/merchant → top-up → transfer → merchant payment (limits + risk + ledger + notification + audit) → idempotent retry → payment held for review → QR payment → insufficient-funds failure → partial refund → over-refund blocked → admin approves held payment → wallet freeze → report → integrity checks → audit trail.

## Architecture

```
Presentation   Console, Menu, Formatter, Customer/Merchant/Admin controllers, Demo
     │  calls only services
Services       AuthService, PaymentService, RefundService, AdminService, QueryService,
     │         ReportService, SettlementService, AccessControl, AuditLogger, Notifier
     │  uses
Domain         Money, Wallet, User/Customer/Merchant/Admin, Transaction, LedgerEntry,
     │         RefundRequest, AuditEvent, PaymentMethod (+4 impls), PaymentIdentifier, Result
Policies       FraudRule/FraudEngine, LimitPolicy        Notifications  NotificationService (+5 impls), EventPublisher
     ↑  implemented by
Repositories   I*Repository / Database  ←  InMemory*  ←  JsonDatabase     Infrastructure  Config, Json, Clock, Logger, PasswordHasher, IdGenerator
App            AppContext (composition root: wires everything by constructor injection, no globals/singletons)
```

Dependencies point inward: the domain knows nothing about JSON, files or the CLI; services depend on repository *interfaces*. Details: [docs/architecture/architecture.md](docs/architecture/architecture.md). Diagrams (Mermaid): [docs/uml/diagrams.md](docs/uml/diagrams.md).

## OOP concepts demonstrated

| Concept | Where (this project) |
|---|---|
| Encapsulation | `Wallet::balance_` private, only `credit`/`debit` mutate it; `Transaction` guards status/refund totals behind `transitionTo`/`applyRefund`; `Credentials` hides hash/salt; `Money` keeps its invariants (non-negative, bounded) |
| Abstraction | `PaymentMethod`, `FraudRule`, `LimitPolicy`, `NotificationService`, `PasswordHasher`, `IClock`, `I*Repository`/`Database` |
| Inheritance (only for real *is-a*) | `Customer`/`Merchant`/`Admin` are `User`s (login, status, credentials shared); interface implementations; `JsonDatabase` is an `InMemoryDatabase` that adds load/flush |
| Polymorphism | `PaymentService` calls `PaymentMethod::authorize` without knowing bank vs card vs cash; `FraudEngine` loops over `FraudRule`s; `EventPublisher` notifies every `NotificationService`; services use `Database&` regardless of storage |
| Composition | Services are composed of injected collaborators; `FraudEngine` owns rules; `User` owns `Credentials`; `Transaction` owns `TransactionData` |
| SOLID | SRP (e.g. `SettlementService` only moves money + posts ledger); OCP (new rule/channel/method = new class, no edits to the pipeline); LSP (any `PaymentMethod`/`FraudRule` substitutable); ISP (narrow repository interfaces); DIP (services depend on abstractions, wired in `AppContext`) |

Payment *types* are deliberately **not** a class hierarchy (`WalletTransfer`, `MerchantPayment`, …): they differ in data (who is paid, which ledger side is external), not behaviour, so one `PaymentCommand` + `TxType` enum + routing function is simpler and avoids a fragile hierarchy. See [docs/design/oop-design.md](docs/design/oop-design.md).

## Design patterns actually used

Strategy (`FraudRule`, `LimitPolicy`), Observer (`EventPublisher` → `NotificationService`s), Repository (`I*Repository`), Factory (`PaymentMethodFactory`, `LimitPolicyFactory`), a lightweight State machine (transition table in `Transaction`), Composite-style aggregation (`FraudEngine`). No Singleton. Rationale and trade-offs: [docs/design/design-patterns.md](docs/design/design-patterns.md).

## Data structures & algorithms

| Need | Structure / algorithm | Complexity |
|---|---|---|
| user / wallet / transaction / idempotency-key lookup | `unordered_map` | O(1) average |
| transaction store | `vector` ordered by time + hash indexes | append O(1) amortised |
| date-range search | binary search (`lower_bound`/`upper_bound`) on the time-ordered vector | O(log n + k) |
| per-user window (daily limit, velocity) | per-user position index + `partition_point` | O(log m + k) |
| velocity rule | sliding window scanned from newest entry | O(k), k = attempts in window |
| fraud review queue | `priority_queue` with lazy deletion (risk desc, age asc) | push O(log n), remove O(1) |
| notification delivery | `queue` | O(1) per event |
| top merchants / busiest users | hash aggregation + `partial_sort` | O(n + g log k) |
| ledger reconciliation | linear sum per wallet | O(entries of wallet) |

Full discussion: [docs/design/dsa-and-complexity.md](docs/design/dsa-and-complexity.md). **No benchmarks have been run: performance numbers are Not measured yet.**

## Project structure

```
Makefile  README.md  LICENSE  .gitignore
config/app.json            tunable limits / thresholds / lockout / hashing iterations
include/<layer>/*.h        public headers per layer (domain, services, policies, notifications,
src/<layer>/*.cpp          repositories, infrastructure, presentation, app, utils)
src/main.cpp               argument parsing + start-up only
tests/                     dependency-free test harness + 5 test files
scripts/                   build.ps1, sample_session.txt
docs/                      architecture, design, uml, testing, viva, requirements
data/                      runtime JSON files (git-ignored)
```

## Testing

`mingw32-make test` runs every test; a single suite can be selected with a name filter, e.g. `./build/wallet_tests.exe refund`. See [docs/testing/testing.md](docs/testing/testing.md) for the suite list and the recorded results.

## Security notes

Educational simulation; not production-grade financial security. See [docs/design/security.md](docs/design/security.md) for what is implemented (salted PBKDF2 hashing, constant-time compare, lockout, RBAC, input validation, idempotency, audit trail, no secrets in logs) and what is not (transport security, MFA, real key management, rate limiting, compliance).

## Limitations

- Single-threaded; no locking. Two simultaneous requests are not supported (see "future").
- Persistence rewrites a whole JSON file when its repository changed: fine for classroom data sizes, O(n) per commit, not for large data. There is no multi-process locking.
- If `commit()` fails after in-memory state changed, memory is ahead of disk until the next successful commit (the operation reports `PersistenceFailure`).
- Notification inboxes are in memory (session only); email/SMS/push only append lines to `data/outbox.log`.
- Limit changes made by the admin at runtime are not written back to `config/app.json`.
- Payment methods are simulated; the amounts "available" on cards/accounts are fake. Single currency in practice (INR; `Currency` supports INR/USD but no conversion).
- PBKDF2 and SHA-256 are implemented in-repo from the public specs (and checked against published test vectors) for dependency-free builds; a vetted library should be used for anything real.

## Future enhancements

SQLite repositories behind the existing interfaces; mutex-protected payment processing (double-spend race analysis); transaction graph analysis; real QR image generation; REST API / web dashboard; event bus and metrics; CMake build alongside the Makefile.

## License

MIT — see [LICENSE](LICENSE).
