# Design patterns actually used

For each: problem → naive design → why it hurts → pattern solution → trade-offs.

## Strategy — `FraudRule`, `LimitPolicy`
* **Problem**: risk and limit checks change often and differ per customer tier.
* **Naive**: `if (amount > X) … else if (recent > Y) …` inside `PaymentService`.
* **Pain**: every new rule edits the payment code; rules cannot be unit-tested alone; tiers multiply branches.
* **Solution**: `FraudRule::evaluate(RiskContext) → RuleResult`; `FraudEngine` runs all rules and takes the strictest decision (`LargeAmountRule`, `VelocityRule`, `RepeatedFailureRule`). `LimitPolicy::check(LimitContext)`.
* **Trade-off**: more small types; rules must share a context struct, so a rule needing new data forces a context change.

## Observer — `EventPublisher` + `NotificationService`
* **Problem**: one payment event must reach in-app inbox, email, SMS, push, console.
* **Naive**: `PaymentService` calls each channel.
* **Pain**: coupling to every channel; new channel = edit payment code; slow channel delays payment.
* **Solution**: services call `Notifier::toUser` → `EventPublisher::publish` (queued); `dispatch()` fans out to all subscribers.
* **Trade-off**: delivery is synchronous at `dispatch()` (no retries/ordering guarantees); the in-app inbox is volatile.

## Repository — `I*Repository`, `Database`
* **Problem**: services need data without caring where it lives.
* **Naive**: models write their own JSON; services open files.
* **Pain**: domain tied to a format, untestable without disk, hard to add SQLite.
* **Solution**: interfaces in `repositories/Repositories.h`; `InMemory*` for tests; `JsonDatabase` for runtime; `commit()` as the persistence seam.
* **Trade-off**: copies returned for wallets/transactions cost allocations; JSON rewrites whole files.

## Factory — `PaymentMethodFactory`, `LimitPolicyFactory`
* Creates a `PaymentMethod` from a kind string + reference with validation (card/account number format) so the CLI never `new`s concrete types; `LimitPolicyFactory::forTier` builds the policy from the *current* config (so admin changes apply immediately).

## Lightweight State machine — `Transaction::canTransition`
* A table of legal moves replaces eight state classes. `transitionTo` rejects illegal moves (`Success → Failed`, `Refunded → anything`). Chosen over the full State pattern because states have no behaviour of their own — only legal successors.

## Composite-style aggregation — `FraudEngine`
* The engine is itself usable wherever "an assessment" is needed and holds many rules; it combines results (max decision, capped score sum).

## Not used
* **Singleton** — all collaborators are injected through `AppContext`; no global state, easy to run several isolated contexts in tests (every test makes its own).
