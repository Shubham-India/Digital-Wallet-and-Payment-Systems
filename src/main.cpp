// Entry point only: parse arguments, build the object graph (AppContext), start the UI.
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#define WALLET_ISATTY(fd) (_isatty(fd) != 0)
#else
#include <unistd.h>
#define WALLET_ISATTY(fd) (isatty(fd) != 0)
#endif

#include "app/AppContext.h"
#include "presentation/Controllers.h"
#include "presentation/Demo.h"

namespace {
void usage() {
    std::cout << "Digital Wallet & Payment System (educational simulator)\n"
                 "Usage: wallet [options]\n"
                 "  --demo            run the scripted end-to-end demo (uses a fresh data/demo directory)\n"
                 "  --data <dir>      data directory for JSON files (default: data)\n"
                 "  --config <file>   configuration file (default: config/app.json)\n"
                 "  --help            show this help\n"
                 "Default admin on first start: " << wallet::kDefaultAdminEmail << " / " << wallet::kDefaultAdminPassword
              << "  (demo credential - change it)\n";
}
}  // namespace

int main(int argc, char** argv) {
    wallet::AppOptions options;
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { usage(); return 0; }
        else if (arg == "--demo") demo = true;
        else if (arg == "--data" && i + 1 < argc) options.dataDir = argv[++i];
        else if (arg == "--config" && i + 1 < argc) options.configPath = argv[++i];
        else { std::cerr << "Unknown or incomplete option: " << arg << "\n"; usage(); return 2; }
    }
    if (demo) {
        options.dataDir = "data/demo";
        options.consoleNotifications = true;
        std::error_code ec;
        std::filesystem::remove_all(options.dataDir, ec);   // the demo always starts from a clean slate
    }

    wallet::SystemClock clock;
    auto ctx = wallet::AppContext::create(options, clock);
    if (!ctx) {
        std::cerr << "Cannot start: " << ctx.error().message << "\n"
                  << "Fix or move the offending file in '" << options.dataDir << "' (it has NOT been modified).\n";
        return 1;
    }
    if (demo) return wallet::runDemo(*ctx.value(), std::cout);

    wallet::Console console(std::cin, std::cout, WALLET_ISATTY(0));
    wallet::Application(*ctx.value(), console).run();
    return 0;
}
