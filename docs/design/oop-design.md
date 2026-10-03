# OOP design — mapped to this code base

## Encapsulation
| Class | Hidden state | Only way to change it |
|---|---|---|
| `Wallet` | `balance_`, `status_` | `credit`, `debit`, `freeze`, `unfreeze`, `close` — each validates status, currency, amount and funds, returning `Result<void>` |
| `Transaction` | `TransactionData d_` (status, refunded amount, failure info) | `transitionTo` (checked against `canTransition`), `markFailed`, `applyRefund` (cap check), `setRouting`, `setMeta` |
| `Credentials` | salt, hash, failed attempts | `recordFailure`, `reset`; hashing is done outside by `PasswordHasher` |
| `User` | status, credentials | `recordFailedLogin(max)` locks the account; `setStatus` (admin) resets attempts when re-activating |
| `Money` | `minor_`, `currency_` | immutable; every operation returns a new value and enforces `0 ≤ amount ≤ kMaxMinor` |

Wallets are handed out by the repository as **copies**; a change only becomes real when `IWalletRepository::save` is called. `SettlementService` exploits this to make a transfer all-or-nothing.

## Abstraction
`PaymentMethod`, `FraudRule`, `LimitPolicy`, `NotificationService`, `PasswordHasher`, `IClock`, `IUserRepository`, `IWalletRepository`, `ITransactionRepository`, `ILedgerRepository`, `IRefundRepository`, `IAuditRepository`, `Database`. Each exists because there are (or will be) several interchangeable implementations or because tests substitute one (`ManualClock`, `InMemoryDatabase`).

## Inheritance — only where it is a real *is-a*
* `Customer`, `Merchant`, `Admin` **are** `User`s: they log in, have credentials, status and an id; services (`AuthService`, `AccessControl`, repositories) treat them uniformly. Role-specific data (tier / business name & payment id) is in the subclass. `User::describe()` and `role()` are virtual.
* Interface implementations (`BankAccount : PaymentMethod`, …).
* `JsonDatabase : InMemoryDatabase` adds disk load/flush to the same working set.

### Where inheritance was deliberately **not** used
* **Payment types** (`WalletTransfer`, `MerchantPayment`, `QRPayment`, `BillPayment`). They do not differ in behaviour: each debits one side, credits the other, runs the same limits/risk/idempotency/ledger steps. They differ only in *who the counterparty is* and *which ledger side is external*, which is data → `TxType` enum + `resolveRouting`. A hierarchy would copy the pipeline or force awkward template-method hooks.
* **Limit tiers** (`BasicLimitPolicy`, `PremiumLimitPolicy`, …): they differ only in numbers → one `TierLimitPolicy` configured from `AppConfig`. The `LimitPolicy` interface remains so a different *kind* of policy can be added without touching `PaymentService`.
* **Transaction states** (State pattern classes): a transition table is 15 lines and easier to verify than eight state classes.

## Polymorphism (runtime)
* `PaymentService::process` → `cmd.method->authorize(amount)`: bank / debit / credit / cash behave differently (balance vs credit limit vs deposit cap) behind one call.
* `FraudEngine::assess` → `rule->evaluate(ctx)` over `vector<unique_ptr<FraudRule>>`.
* `EventPublisher::dispatch` → `subscriber->notify(n)` for Email/SMS/Push/Console/In-app.
* `AppContext` stores a `unique_ptr<Database>`; every service calls `db.users()`, `db.commit()` with no idea if it is `InMemoryDatabase` or `JsonDatabase`.

## Composition
Services own nothing by inheritance; they are assembled from injected references (`PaymentService::Dependencies`). `FraudEngine` owns its rules; `User` owns `Credentials`; `Transaction` owns its data; `AppContext` owns the whole graph.

Why composition: behaviour can be recombined without new subclasses (e.g. run the payment pipeline with a different engine in a test), there is no fragile base class, and dependencies are explicit in constructors.

## SOLID, honestly applied
* **S** — `SettlementService` only moves money and posts ledger lines; `AuditLogger` only records events; `AccessControl` only decides permissions.
* **O** — adding a fraud rule, channel, payment method or repository backend is a new class plus one registration line in `AppContext`/factory; `PaymentService` is untouched.
* **L** — all `PaymentMethod`/`FraudRule`/`NotificationService` implementations honour the same contract (no strengthened preconditions; failures via `Result`). `InMemoryDatabase` ↔ `JsonDatabase` are substitutable.
* **I** — repositories are split per aggregate instead of one huge storage interface; `AccessControl` depends on `SessionManager` read-only.
* **D** — services depend on `Database&` and interfaces; concrete classes appear only in `AppContext`.

Known compromises: `PaymentService::Dependencies` has 11 references (a sign the service is large — `execute/process/finalize` could be split into a pipeline of step objects); `IdGenerator` and `Logger` are used as concrete classes rather than interfaces because no second implementation is needed (YAGNI).
