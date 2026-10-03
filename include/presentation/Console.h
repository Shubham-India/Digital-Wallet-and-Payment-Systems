#pragma once
#include <functional>
#include <iosfwd>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "domain/Money.h"

namespace wallet {

// All terminal I/O goes through this class so controllers are testable with string streams
// and never touch std::cin/std::cout directly. It also never throws on bad input.
class Console {
public:
    Console(std::istream& in, std::ostream& out, bool interactiveTerminal) : in_(in), out_(out), tty_(interactiveTerminal) {}

    std::ostream& out() { return out_; }
    bool eof() const noexcept { return eof_; }

    void banner(const std::string& title);
    void section(const std::string& title);
    void info(const std::string& msg);
    void success(const std::string& msg);
    void error(const std::string& msg);

    std::string prompt(const std::string& label);          // trimmed line; "" on EOF
    std::string promptSecret(const std::string& label);    // hides input on a real terminal
    std::optional<Money> promptMoney(const std::string& label);   // prints the parse error itself
    bool confirm(const std::string& question);             // y/n, default no
    void pause();

private:
    bool readLine(std::string& line);
    std::istream& in_;
    std::ostream& out_;
    bool tty_;
    bool eof_ = false;
};

struct MenuItem {
    std::string key;
    std::string label;
    std::function<void()> action;
};

// Data-driven menu: adding a feature = adding an item, not growing a switch statement.
class Menu {
public:
    explicit Menu(std::string title) : title_(std::move(title)) {}
    Menu& add(std::string key, std::string label, std::function<void()> action);
    // Loops until the user picks `exitKey` (or input ends). `header` is printed above the items each time.
    void run(Console& console, const std::string& exitKey, const std::string& exitLabel,
             const std::function<std::string()>& header = nullptr) const;
private:
    std::string title_;
    std::vector<MenuItem> items_;
};

} // namespace wallet
