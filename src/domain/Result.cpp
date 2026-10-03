#include "domain/Result.h"

namespace wallet {

const char* toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidAmount: return "InvalidAmount";
        case ErrorCode::InsufficientFunds: return "InsufficientFunds";
        case ErrorCode::WalletFrozen: return "WalletFrozen";
        case ErrorCode::WalletClosed: return "WalletClosed";
        case ErrorCode::WalletNotFound: return "WalletNotFound";
        case ErrorCode::UserNotFound: return "UserNotFound";
        case ErrorCode::MerchantNotFound: return "MerchantNotFound";
        case ErrorCode::TransactionNotFound: return "TransactionNotFound";
        case ErrorCode::RefundNotFound: return "RefundNotFound";
        case ErrorCode::NotAuthenticated: return "NotAuthenticated";
        case ErrorCode::Unauthorized: return "Unauthorized";
        case ErrorCode::InvalidCredentials: return "InvalidCredentials";
        case ErrorCode::AccountLocked: return "AccountLocked";
        case ErrorCode::AccountInactive: return "AccountInactive";
        case ErrorCode::DuplicateUser: return "DuplicateUser";
        case ErrorCode::ValidationFailed: return "ValidationFailed";
        case ErrorCode::LimitExceeded: return "LimitExceeded";
        case ErrorCode::FraudRejected: return "FraudRejected";
        case ErrorCode::IdempotencyMismatch: return "IdempotencyMismatch";
        case ErrorCode::RefundExceedsOriginal: return "RefundExceedsOriginal";
        case ErrorCode::RefundNotAllowed: return "RefundNotAllowed";
        case ErrorCode::DuplicateRefundRequest: return "DuplicateRefundRequest";
        case ErrorCode::InvalidTransition: return "InvalidTransition";
        case ErrorCode::InvalidPaymentMethod: return "InvalidPaymentMethod";
        case ErrorCode::InvalidIdentifier: return "InvalidIdentifier";
        case ErrorCode::SelfTransfer: return "SelfTransfer";
        case ErrorCode::CurrencyMismatch: return "CurrencyMismatch";
        case ErrorCode::PersistenceFailure: return "PersistenceFailure";
        case ErrorCode::Overflow: return "Overflow";
    }
    return "Unknown";
}

} // namespace wallet
