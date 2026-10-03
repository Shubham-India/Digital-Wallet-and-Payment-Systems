# Resume material (facts verified against the code and test run)

**Project title:** Digital Wallet & Payment System Simulator (C++20, OOP)

**Bullets** (only implemented, verified functionality — no usage, throughput or accuracy claims, none were measured):

* Designed and implemented a layered C++20 wallet/payment simulator (domain, services, repositories, CLI) with an integer-based `Money` type, a double-entry ledger with reconciliation checks, all-or-nothing settlement, and per-user idempotency keys that prevent duplicate charges on retried requests.
* Built pluggable rule engines using the Strategy pattern — velocity (sliding-window), large-amount and repeated-failure fraud rules feeding a priority review queue, plus configurable per-tier transaction limits — together with RBAC, account lockout, salted PBKDF2 password hashing, an append-only audit trail and an Observer-based multi-channel notification system.
* Implemented full/partial refunds linked to original transactions, JSON persistence behind repository interfaces (atomic file replace, corruption detection), report/CSV export, and a role-based CLI; verified with a dependency-free automated test suite (83 tests / see `docs/testing/testing.md` for the recorded run) and a scripted end-to-end demo.

Avoid adding numbers (users, TPS, latency, coverage percentages) unless you measure them first.
