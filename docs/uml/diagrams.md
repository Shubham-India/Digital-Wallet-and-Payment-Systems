# UML / diagrams (Mermaid — render on GitHub or any Mermaid viewer)

All diagrams show classes that exist in `include/`.

## 1. Use cases

```mermaid
flowchart LR
    C([Customer]) --> UC1(Register / Login)
    C --> UC2(Add money)
    C --> UC3(Transfer / Pay merchant / QR / Bill)
    C --> UC4(View history, ledger)
    C --> UC5(Request refund)
    M([Merchant]) --> UC1
    M --> UC6(Receive payments, generate QR id)
    M --> UC7(Issue refund / resolve refund requests)
    M --> UC8(Withdraw settlement)
    A([Admin]) --> UC1
    A --> UC9(List users, freeze wallets, unlock accounts)
    A --> UC10(Review fraud queue)
    A --> UC11(Audit log, reports, CSV export)
    A --> UC12(Configure limits)
    UC3 -.includes.-> UC13(Limit check)
    UC3 -.includes.-> UC14(Risk assessment)
    UC3 -.includes.-> UC15(Ledger posting)
    UC3 -.includes.-> UC16(Notification + audit)
```

## 2. Architecture

```mermaid
flowchart TB
    subgraph Presentation
      Console --- Menu
      CustomerController
      MerchantController
      AdminController
      Formatter
    end
    subgraph Services
      AuthService
      PaymentService
      RefundService
      AdminService
      QueryService
      ReportService
      SettlementService
      AccessControl
      AuditLogger
      Notifier
    end
    subgraph Domain
      Money
      Wallet
      User
      Transaction
      PaymentMethod
    end
    subgraph Policies
      FraudEngine
      LimitPolicy
    end
    subgraph Repositories
      Database["Database / I*Repository"]
      InMemoryDatabase
      JsonDatabase
    end
    Presentation --> Services
    Services --> Domain
    Services --> Policies
    Services --> Database
    JsonDatabase --> InMemoryDatabase
    InMemoryDatabase -.implements.-> Database
    AppContext["AppContext (composition root)"] --> Services
```

## 3. Core domain classes

```mermaid
classDiagram
    class Money { -int64 minor_ -Currency currency_ +fromMinor() +parse() +operator+() +operator-() }
    class Wallet { -string id_ -Money balance_ -WalletStatus status_ +credit() +debit() +freeze() +unfreeze() +close() }
    class User { <<abstract>> -string id_ -Credentials credentials_ -AccountStatus status_ -string walletId_ +role()* +describe()* +recordFailedLogin() +setStatus() }
    class Customer { -AccountTier tier_ }
    class Merchant { -string businessName_ -string paymentId_ }
    class Admin
    class Credentials { -string salt_ -string hash_ -int failedAttempts_ }
    class Transaction { -TransactionData d_ +transitionTo() +markFailed() +applyRefund() +refundable() }
    class LedgerEntry { +string walletId +EntryDirection direction +Money amount +Money balanceAfter }
    class RefundRequest { +string transactionId +Money amount +RefundRequestStatus status }
    class AuditEvent { +string actor +string action +string target +string result }
    User <|-- Customer
    User <|-- Merchant
    User <|-- Admin
    User *-- Credentials
    User "1" --> "0..1" Wallet : walletId
    Wallet *-- Money
    Transaction *-- Money
    Transaction "1" --> "0..*" LedgerEntry : settled as
    Transaction "0..1" --> "0..*" Transaction : linkedTxId (refunds)
    RefundRequest --> Transaction
```

## 4. Payment processing

```mermaid
classDiagram
    class PaymentService {
      +addMoney() +withdraw() +transfer() +payMerchant() +payQr() +payBill()
      +approveReview() +rejectReview()
      -execute() -resolveRouting() -process() -finalize() -abort()
    }
    class PaymentCommand { +idempotencyKey +TxType type +counterparty +Money amount +PaymentMethod* method }
    class PaymentOutcome { +Transaction tx +bool duplicate +RiskAssessment risk }
    class SettlementService { +settle() +reconciles() +balanced() }
    class AccessControl { +authorize() +roleHas()$ }
    class PaymentMethod { <<interface>> +kind() +describe() +authorize() }
    class BankAccount
    class DebitCard
    class CreditCard
    class CashDeposit
    class PaymentMethodFactory { +create()$ }
    PaymentService ..> PaymentCommand
    PaymentService ..> PaymentOutcome
    PaymentService --> SettlementService
    PaymentService --> AccessControl
    PaymentService --> FraudEngine
    PaymentService --> LimitPolicyFactory
    PaymentService --> Database
    PaymentService --> Notifier
    PaymentService --> AuditLogger
    PaymentMethod <|.. BankAccount
    PaymentMethod <|.. DebitCard
    PaymentMethod <|.. CreditCard
    PaymentMethod <|.. CashDeposit
    PaymentMethodFactory ..> PaymentMethod
```

## 5. Transfer sequence

```mermaid
sequenceDiagram
    actor U as Customer (CLI)
    participant P as PaymentService
    participant AC as AccessControl
    participant R as Database (repos)
    participant L as LimitPolicy
    participant F as FraudEngine
    participant S as SettlementService
    participant N as Notifier/EventPublisher
    participant A as AuditLogger
    U->>P: transfer(session, toEmail, amount, key)
    P->>AC: authorize(Transfer)
    P->>R: findByScopedKey(userId:key)
    alt key already used
      R-->>P: existing tx
      P-->>U: original outcome (duplicate=true)
    else new request
      P->>R: save Transaction(PENDING) after routing
      P->>L: check(amount, spentToday, countToday)
      P->>F: assess(recent attempts)
      alt REJECT
        P->>R: tx FAILED
        P->>A: record(SUSPICIOUS)
        P-->>U: FraudRejected
      else REVIEW
        P->>R: tx PENDING_REVIEW (queue)
        P-->>U: held for review
      else APPROVE
        P->>S: settle(tx)
        S->>R: debit/credit wallet copies, save wallets, append 2 ledger entries
        P->>R: tx SUCCESS
        P->>A: record(SUCCESS)
        P->>N: publish + dispatch
        P->>R: commit()
        P-->>U: Success
      end
    end
```

## 6. Merchant payment + refund sequence

```mermaid
sequenceDiagram
    actor C as Customer
    actor M as Merchant
    participant P as PaymentService
    participant RS as RefundService
    participant S as SettlementService
    C->>P: payMerchant(merchantPaymentId, amount, key)
    P->>P: resolve merchant by paymentId, limits, risk
    P->>S: settle: customer wallet → merchant wallet
    P-->>C: SUCCESS (TX-A)
    M->>RS: issueRefund(TX-A, partial amount, key)
    RS->>RS: check actor is payee, status, amount ≤ refundable
    RS->>S: settle: merchant wallet → customer wallet (new REFUND tx linked to TX-A)
    RS->>RS: TX-A.applyRefund → PARTIALLY_REFUNDED / REFUNDED
    RS-->>M: refund transaction
```

## 7. Transaction lifecycle

```mermaid
stateDiagram-v2
    [*] --> Pending
    Pending --> PendingReview : risk = REVIEW
    Pending --> Processing : approved
    Pending --> Failed : validation/limit/funds/risk reject
    Pending --> Cancelled
    PendingReview --> Processing : admin approves
    PendingReview --> Cancelled : admin rejects
    PendingReview --> Failed
    Processing --> Success : ledger posted
    Processing --> Failed : settlement error
    Success --> PartiallyRefunded : partial refund
    Success --> Refunded : full refund
    PartiallyRefunded --> PartiallyRefunded : another partial refund
    PartiallyRefunded --> Refunded : remainder refunded
    Failed --> [*]
    Cancelled --> [*]
    Refunded --> [*]
```

## 8. Fraud / risk engine

```mermaid
classDiagram
    class FraudEngine { -vector~unique_ptr~FraudRule~~ rules_ +addRule() +assess(RiskContext) RiskAssessment +standard(FraudSettings)$ }
    class FraudRule { <<interface>> +evaluate(RiskContext) RuleResult +name() }
    class LargeAmountRule { -Money reviewAt_ -Money rejectAt_ }
    class VelocityRule { -int window_ -int reviewCount_ -int rejectCount_ }
    class RepeatedFailureRule { -int window_ -int reviewCount_ }
    class FraudReviewQueue { -priority_queue heap_ -unordered_set active_ +push() +remove() +top() +pending() +rebuildFrom() }
    FraudRule <|.. LargeAmountRule
    FraudRule <|.. VelocityRule
    FraudRule <|.. RepeatedFailureRule
    FraudEngine o-- FraudRule
    FraudEngine ..> RiskAssessment
    PaymentService --> FraudEngine
    PaymentService --> FraudReviewQueue
```

## 9. Notification system

```mermaid
classDiagram
    class NotificationService { <<interface>> +channel() +notify(Notification) }
    class ConsoleNotification
    class EmailNotification
    class SmsNotification
    class PushNotification
    class InAppNotification { +inboxFor() +unreadCount() }
    class EventPublisher { -queue~Notification~ -vector~shared_ptr~ subscribers_ +subscribe() +publish() +dispatch() }
    class Notifier { +toUser() +flush() }
    NotificationService <|.. ConsoleNotification
    NotificationService <|.. EmailNotification
    NotificationService <|.. SmsNotification
    NotificationService <|.. PushNotification
    NotificationService <|.. InAppNotification
    EventPublisher o-- NotificationService
    Notifier --> EventPublisher
```

## 10. Repository architecture

```mermaid
classDiagram
    class Database { <<interface>> +users() +wallets() +transactions() +ledger() +refunds() +audit() +commit() }
    class IUserRepository { <<interface>> }
    class IWalletRepository { <<interface>> }
    class ITransactionRepository { <<interface>> }
    class ILedgerRepository { <<interface>> }
    class IRefundRepository { <<interface>> }
    class IAuditRepository { <<interface>> }
    class InMemoryDatabase
    class JsonDatabase { +load() +commit() }
    Database <|.. InMemoryDatabase
    InMemoryDatabase <|-- JsonDatabase
    Database --> IUserRepository
    Database --> IWalletRepository
    Database --> ITransactionRepository
    Database --> ILedgerRepository
    Database --> IRefundRepository
    Database --> IAuditRepository
```

## Simplified diagram for reports / viva

```mermaid
flowchart LR
    CLI --> Services --> Domain
    Services --> Rules["Fraud + Limit rules"]
    Services --> Repo["Repository interfaces"] --> JSON[(JSON files)]
    Services --> Notify["Notification channels"]
    Services --> Audit[(Audit log)]
```
