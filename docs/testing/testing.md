# Testing

Framework: a small dependency-free harness (`tests/TestFramework.h`: `TEST`, `CHECK`, `CHECK_EQ`) because Catch2/GoogleTest are not available on the target toolchain (no package manager, no CMake). Tests run against `AppContext` with an in-memory database and a `ManualClock`; persistence tests use a real directory under `build/test-data/`.

```bash
mingw32-make test                        # build + run everything
./build/wallet_tests.exe refund          # run only tests whose name contains "refund"
powershell -File scripts/build.ps1       # alternative without make
```

## Recorded result (this repository, g++ 16.1.0 MSYS2 UCRT64, Windows 11, `-O2`, 2026-10-04)

```
83 tests run, 83 passed, 0 failed (564 checks)
```
Also verified: clean rebuild with `-Wall -Wextra -Wpedantic` (no warnings), `make debug`, `scripts/build.ps1`, `wallet --demo` exit code 0, and a piped interactive CLI session (`scripts/sample_session.txt`).
Code coverage: **Not measured yet.**

## Suites

| File | Tests | Covers |
|---|---|---|
| `tests/test_core.cpp` | 19 | `Money` (parsing, rejects, no float drift, arithmetic, invariant exceptions), `Wallet` (credit, debit, insufficient, freeze/unfreeze/close), `Transaction` state machine and refund accounting, SHA-256 and PBKDF2 published test vectors, password hasher, JSON parser/serializer, QR identifier round-trip and malformed input, time formatting, id generator |
| `tests/test_auth.cpp` | 13 | registration (wallet created, hash stored not password), validation, duplicate email, login/logout, generic invalid-credential errors, lockout + admin unlock, counter reset, suspended user, admin bootstrap, RBAC matrix and enforcement in services, logged-out/forged sessions, audit contains no secrets |
| `tests/test_payments.cpp` | 34 | top-up via all four methods, factory + masking, cash cap, **scenario 1** transfer, **scenario 2** insufficient funds (no change, FAILED recorded, no ledger lines), unknown/self/admin/frozen/suspended counterparty, invalid amount/key, withdraw, **scenario 3** idempotent retry, key mismatch, per-user key scope, failed-request replay, **scenario 4** per-transaction limit, premium tier, daily amount (+ next-day reset), daily count (admin-configured), **scenario 5** review hold, admin approve/reject, review-queue ordering, fraud rules (large amount, sliding-window velocity, engine combination, rapid transfers → review → reject, repeated failures), **scenario 6** merchant payment, QR payment, bill payment, ledger double-entry + wallet reconciliation, atomic settlement when 2nd leg fails, notifications + audit, notification channel formatting, search/filter + authorization scoping |
| `tests/test_refunds_reports_persistence.cpp` | 17 | **scenario 7** full refund + linkage, partial refunds + cap, duplicate refund (key and cap), authorization/validation, refund when merchant lacks funds, customer request → merchant approve/reject, reports (summary, top merchants, highest, per-day, busiest, text, CSV), admin operations audited, persistence round-trip across restarts (users, wallets, transactions, ledger, audit, hashes, id continuity, idempotency record), review queue rebuilt after restart, corrupt file reported and untouched, invalid record rejected, JSON repository round-trip incl. escapes, repository ordering/update behaviour, user uniqueness, full end-to-end flow (register → top-up → transfer → merchant payment → review → approve → partial refund → invariants → report → audit) |

## Mapping to the required scenarios
1 Alice→Bob transfer: `scenario1_*`. 2 insufficient funds: `scenario2_*`. 3 same key twice: `scenario3_*`. 4 limit exceeded: `scenario4_*`, `daily_*`. 5 suspicious → review: `scenario5_*`, `fraud_*`. 6 merchant payment: `scenario6_*`. 7 merchant refund: `scenario7_*`, `partial_refunds_*`, `duplicate_refund_*`.

## Not covered / known gaps
* Concurrency (not implemented, so not tested).
* The interactive CLI is verified by running scripted stdin sessions manually, not by automated tests.
* No fuzzing, no sanitizer run, no load/performance tests (**Not measured yet**).

## Bug found by testing
`Result<T>::value() &&` initially returned `T&&`; compiling the tests under `-Wall` flagged a dangling reference in `for (auto& x : service.call().value())`. It now returns by value.
