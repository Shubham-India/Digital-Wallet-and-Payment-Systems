# Error handling and money handling

## Which mechanism and why
| Situation | Mechanism | Reason |
|---|---|---|
| Expected business failure (insufficient funds, frozen wallet, limit exceeded, unknown user, fraud rejection, duplicate refund, bad input) | `Result<T>` / `Result<void>` with `Error{ErrorCode, message}` | Failures are normal outcomes; the caller must handle them; no stack unwinding cost; easy to test (`r.error().code == …`). |
| Programmer errors / violated invariants (negative `Money`, overflow, mixed currency, `value()` on a failed `Result`) | `WalletException` | Cannot be "handled" meaningfully; makes misuse loud. |
| Persistence faults | `Result` with `PersistenceFailure`; start-up refuses to run on corrupt data | The user needs a clear message and the file must not be overwritten. |
| CLI | menu loop catches `std::exception` as a last resort so a bug cannot kill a session | Defence in depth. |

`Result<T>` is a small in-repo `std::expected` look-alike (C++23 feature, unavailable on a C++20 baseline). `fail(code, message)` converts to any `Result<T>`.

No domain class prints anything; only presentation prints.

## Why not `double` for balances
`0.1 + 0.2 != 0.3` in binary floating point, and errors accumulate. `Money` stores integer **paise** (₹125.50 → 12550) in `int64_t`:
* exact arithmetic, deterministic comparison;
* invariants: never negative, `≤ 10^12` paise, so `a + b` cannot overflow `int64_t` (checked explicitly);
* parsing accepts only `digits[.dd]` (no signs, no exponents, ≤ 2 decimals);
* mixed currencies throw.
Test `money_has_no_floating_point_drift` adds 0.10 ten times and expects exactly 100 paise.
