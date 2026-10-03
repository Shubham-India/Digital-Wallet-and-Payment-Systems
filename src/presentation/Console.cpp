#include "presentation/Console.h"

#include <algorithm>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wallet {

namespace {
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
}  // namespace

void Console::banner(const std::string& title) {
    std::string bar(44, '=');
    out_ << "\n" << bar << "\n";
    std::size_t pad = title.size() < 44 ? (44 - title.size()) / 2 : 0;
    out_ << std::string(pad, ' ') << title << "\n" << bar << "\n";
}

void Console::section(const std::string& title) { out_ << "\n--- " << title << " ---\n"; }
void Console::info(const std::string& msg) { out_ << msg << "\n"; }
void Console::success(const std::string& msg) { out_ << "[OK] " << msg << "\n"; }
void Console::error(const std::string& msg) { out_ << "[!] " << msg << "\n"; }

bool Console::readLine(std::string& line) {
    if (eof_) return false;
    if (!std::getline(in_, line)) {
        eof_ = true;
        return false;
    }
    return true;
}

std::string Console::prompt(const std::string& label) {
    out_ << label << ": " << std::flush;
    std::string line;
    if (!readLine(line)) {
        out_ << "\n";
        return "";
    }
    if (!tty_) out_ << line << "\n";   // echo piped input so transcripts read naturally
    return trim(line);
}

std::string Console::promptSecret(const std::string& label) {
    out_ << label << ": " << std::flush;
    std::string line;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool hidden = tty_ && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) && SetConsoleMode(h, mode & ~ENABLE_ECHO_INPUT);
    bool ok = readLine(line);
    if (hidden) { SetConsoleMode(h, mode); out_ << "\n"; }
#else
    bool hidden = false;
    bool ok = readLine(line);
#endif
    if (!ok) { out_ << "\n"; return ""; }
    if (!tty_ && !hidden) out_ << "********\n";   // never echo a secret into a transcript
    return line;                                   // passwords are not trimmed
}

std::optional<Money> Console::promptMoney(const std::string& label) {
    std::string text = prompt(label);
    if (text.empty()) return std::nullopt;
    auto m = Money::parse(text);
    if (!m) {
        error(m.error().message);
        return std::nullopt;
    }
    return m.value();
}

bool Console::confirm(const std::string& question) {
    std::string a = prompt(question + " (y/N)");
    return a == "y" || a == "Y" || a == "yes";
}

void Console::pause() {
    if (tty_) prompt("Press Enter to continue");
}

Menu& Menu::add(std::string key, std::string label, std::function<void()> action) {
    items_.push_back({std::move(key), std::move(label), std::move(action)});
    return *this;
}

void Menu::run(Console& console, const std::string& exitKey, const std::string& exitLabel,
               const std::function<std::string()>& header) const {
    while (!console.eof()) {
        console.banner(title_);
        if (header) {
            std::string h = header();
            if (!h.empty()) console.info(h);
        }
        for (const auto& item : items_) console.out() << " " << item.key << ". " << item.label << "\n";
        console.out() << " " << exitKey << ". " << exitLabel << "\n";
        std::string choice = console.prompt("Choose");
        if (choice.empty() && console.eof()) return;
        if (choice == exitKey) return;
        auto it = std::find_if(items_.begin(), items_.end(), [&](const MenuItem& i) { return i.key == choice; });
        if (it == items_.end()) {
            console.error("Invalid choice '" + choice + "'. Please pick a number from the menu.");
            continue;
        }
        try {
            it->action();
        } catch (const std::exception& e) {   // last line of defence: a bug must not kill the session
            console.error(std::string("Unexpected error: ") + e.what());
        }
    }
}

} // namespace wallet
