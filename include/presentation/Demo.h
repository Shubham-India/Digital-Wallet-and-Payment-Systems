#pragma once
#include <ostream>

#include "app/AppContext.h"

namespace wallet {

// Scripted end-to-end walkthrough (Alice, Bob, a merchant and the admin). Drives the same services
// the CLI uses, narrating each step. Returns 0 when every step behaved as expected, 1 otherwise.
int runDemo(AppContext& ctx, std::ostream& out);

} // namespace wallet
