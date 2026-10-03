# Data structures, algorithms and complexity

n = transactions stored, m = a user's transactions, k = items in the answer/window, u = users, g = distinct groups.

| Operation | Implementation | Time | Space |
|---|---|---|---|
| Find user by id / email / merchant payment id | three `unordered_map`s | O(1) avg | O(u) |
| Find wallet by id | `unordered_map` id → vector position | O(1) avg | O(w) |
| Find transaction by id; idempotency-key lookup | `unordered_map` id→pos, key→pos | O(1) avg | O(n) |
| Insert transaction (normal case) | `push_back` + index inserts | O(1) amortised | O(1) |
| Insert out-of-order timestamp | sorted insert + index rebuild | O(n) (rare) | — |
| User's transactions since time *t* | per-user position vector + `partition_point` | O(log m + k) | O(n) indexes |
| Search by date range | `lower_bound` / `upper_bound` on time-ordered vector | O(log n + k) | — |
| Search by id | hash lookup | O(1) | — |
| Search by other attributes | scan of the selected candidate set | O(candidates) | — |
| Daily-limit usage | scan of today's user transactions | O(log m + k) | — |
| Velocity rule | reverse scan stopping at window start (sliding window) | O(k) | — |
| Repeated-failure rule | same reverse scan | O(k) | — |
| Push to review queue | `priority_queue::push` + hash set | O(log q) | O(q) |
| Resolve a review item | erase from active set (lazy deletion) | O(1) | — |
| Next review item / list pending | pop stale entries / copy heap and pop | O(log q) amortised / O(q log q) | — |
| Notification fan-out | `std::queue` drained by `dispatch()` | O(subscribers) per event | O(events) |
| Idempotency check | hash lookup on `userId:key` | O(1) avg | O(n) |
| Top merchants / busiest users | hash aggregation then `partial_sort` | O(n + g log top) | O(g) |
| Highest transactions | `partial_sort` on a copy | O(n log top) | O(n) |
| Recent transactions | tail of the time-ordered vector | O(top) + copy O(n) | — |
| Reconcile one wallet | sum its ledger entries | O(entries of wallet) | — |
| JSON `commit()` | rewrite every dirty file | O(size of dirty data) | O(size) |
| Startup `load()` | parse + insert everything | O(total records) | O(total) |

Notes
* `all()` and several report methods copy vectors (simple, safe) — O(n) extra space. Acceptable for the intended data size; an iterator/visitor interface would remove the copies.
* Hash maps give O(1) *average*; the worst case is O(n) with adversarial hashing, irrelevant for generated ids.
* **Graph analysis** (customer→merchant, customer→customer) was not implemented: it would need no new data (payer→payee edges are already in the transactions) but there is no feature that needs it yet. Not forced into the project.
* **No benchmark has been run. Measured performance: Not measured yet.**
