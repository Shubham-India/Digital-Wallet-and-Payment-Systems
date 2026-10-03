#include "domain/Records.h"

namespace wallet {

const char* toString(EntryDirection d) { return d == EntryDirection::Debit ? "DEBIT" : "CREDIT"; }

const char* toString(RefundRequestStatus s) {
    switch (s) {
        case RefundRequestStatus::Pending: return "PENDING";
        case RefundRequestStatus::Approved: return "APPROVED";
        case RefundRequestStatus::Rejected: return "REJECTED";
    }
    return "?";
}

} // namespace wallet
