# Viva preparation — questions and model answers (about *this* project)

## OOP
**Why encapsulation here?** `Wallet::balance_` is private; the only mutators `credit`/`debit` enforce status, currency, positive amount and sufficient funds. Nobody can write `wallet.balance -= x` and bypass the rules. Same for `Transaction` (status only via `transitionTo`) and `Credentials`.

**Where is abstraction?** `PaymentMethod`, `FraudRule`, `LimitPolicy`, `NotificationService`, `Database`/`I*Repository`, `PasswordHasher`, `IClock`. `PaymentService` knows only these interfaces.

**Where is polymorphism?** `cmd.method->authorize(amount)` in `PaymentService::process`: a `BankAccount`, `DebitCard`, `CreditCard` or `CashDeposit` each answer differently. Also `FraudEngine` over `FraudRule`s, `EventPublisher` over `NotificationService`s, and every service over `Database`.

**Why inheritance for `User` but not for payments?** Customer/Merchant/Admin *are* users and are treated uniformly by login, RBAC and storage. Transfer/merchant/QR/bill payments are the same algorithm with different data, so a class hierarchy would only duplicate or complicate the pipeline; an enum plus a routing function is simpler.

**Why composition over inheritance?** Services are assembled from injected collaborators: no fragile base class, easy to swap pieces (tests use `ManualClock` and `InMemoryDatabase`), dependencies are visible in constructors.

**How does SOLID apply?** S: `SettlementService` only settles. O: add a fraud rule = new class + one `addRule`. L: any `PaymentMethod` can replace another. I: repositories are per-aggregate. D: services depend on `Database&`; only `AppContext` names concrete classes.

**Which design patterns and why?** Strategy (fraud rules, limit policies), Observer (notifications), Repository (persistence), Factory (payment methods, limit policy), a transition-table State machine. No Singleton, because dependency injection gives the same sharing without global state.

## Architecture
**Why separate domain and service layers?** The domain holds invariants (a wallet never goes negative); services orchestrate use cases (authorize, check limits, risk, settle, audit). The CLI can change without touching rules.

**Why repositories? Why not DB code in models?** So the domain does not depend on JSON/SQL, tests run in memory, and adding SQLite is a new `Database` implementation, not a rewrite.

**How does dependency injection help?** `AppContext` builds the graph once; every class receives what it needs through its constructor, so each test creates an isolated application with a fake clock.

## Payment system
**How do you prevent double payment?** Idempotency key scoped per user (`userId:key`) with a fingerprint of the request. A retry returns the original transaction (`duplicate=true`), a different payload under the same key is `IdempotencyMismatch`. Additionally the state machine forbids settling a transaction twice.

**How is balance consistency maintained?** Money is integer paise; settlement changes both wallets on copies and only saves them (and appends two ledger entries) if both succeed. Invariants checked in tests: wallet balance = Σcredits − Σdebits; each settled transaction's debits = credits; total money in wallets = total topped up.

**What happens if a transaction fails midway?** Nothing financial has changed (validation, limits, risk and the feasibility probe happen before settlement; settlement is all-or-nothing). The transaction is marked FAILED with a code/reason, an audit event and a notification are written, and no ledger entries exist for it.

**How do refunds work?** A new REFUND transaction moves money merchant → customer, linked to the original. The original tracks `refundedAmount ≤ amount`; status becomes PARTIALLY_REFUNDED/REFUNDED. Over-refund and repeat refunds are rejected; an idempotency key makes retries safe.

**What is the fraud engine, honestly?** A rule-based educational simulation with configurable thresholds (large amount, velocity in a sliding window, repeated failures). It is not a trained or real-world model.

**What is the difference between request, transaction, ledger entry and balance?** Request = what the user asked; transaction = business record of the attempt (including failures); ledger entry = accounting posting; balance = current state derived from postings.

## DSA
**Why `unordered_map`?** Lookup by id/email/idempotency key in O(1) average.
**Where is sorting/searching used?** Reports use `partial_sort` for top-N; the transaction vector is time-ordered so date-range queries use binary search (`lower_bound`/`upper_bound`), and per-user windows use `partition_point`.
**Why a priority queue for suspicious transactions?** Admins should see the riskiest/oldest first; push O(log n); resolved items are removed lazily via a hash set.
**Where is a sliding window?** `VelocityRule`/`RepeatedFailureRule`: scan the user's recent transactions from the newest until the window start, O(k).
**Could a graph model transaction relationships?** Yes — payer→payee edges (customer→merchant, customer→customer) support fan-out/ring analysis with BFS/DFS in O(V+E). Not implemented because no current feature needs it.

## Security
**Why can't `double` be used for money?** Binary floating point cannot represent 0.1 exactly; errors accumulate and comparisons fail. We store integer paise.
**How are passwords stored?** Random per-user salt + PBKDF2-HMAC-SHA256 with many iterations, compared in constant time; plain text is never stored or logged. Educational: production should use Argon2/bcrypt via a vetted library.
**What is idempotency?** Performing the same request many times has the same effect as once — essential when networks retry.
**Authentication vs authorization?** Authentication = who are you (login). Authorization = what may you do (`AccessControl` permission matrix).
**Is this secure enough for real money?** No: no TLS, MFA, rate limiting, key management, encrypted storage or compliance. Educational simulation; not production-grade financial security.

## Concurrency
**What if two payments hit one wallet at the same time?** Not supported: the application is single-threaded. Without protection, both could read balance ₹1000, both approve ₹800/₹700, and overwrite each other (a race condition: a read-modify-write that is not atomic). The fix is a critical section — a per-wallet (or global) mutex around "probe → settle", or optimistic version checks in the database. Left as a documented future module.

## Persistence
**Why JSON and what are the risks?** No dependency, human-readable, easy to inspect. Whole-file rewrites (O(size)), no multi-process locking, unencrypted. Writes use temp-file-then-rename; corrupt files stop start-up and are never overwritten.
