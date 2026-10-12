#pragma once
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lamium::ui::calculator {
// The calculator panel's arithmetic (BACKLOG L-171, docs/demos/calculator.html).
// Pure: numbers, + - * / % ^, parentheses, unit words after a number, a few
// functions and the variables the panel supplies. Nothing leaves the panel.
struct Variables {
    std::optional<double> ans, x, y, z;
};
enum class Error { None, Empty, Incomplete, UnknownName, Undefined };
struct Result {
    std::optional<double> value;
    Error error = Error::None;
    std::string name; // the unknown word, for Error::UnknownName
};
// Storage units (decided 2026-10-12): a stack, a chest (27 stacks, as much as
// a shulker box) and a large chest.
inline std::optional<double> unit(std::string_view word) {
    if (word == "st") return 64;
    if (word == "ch" || word == "sb") return 64 * 27;
    if (word == "lc") return 64 * 54;
    return std::nullopt;
}

namespace detail {
struct Token {
    enum Kind { Number, Word, Symbol, End } kind = End;
    double number = 0;
    std::string text;
};
inline std::vector<Token> tokens(std::string_view source, bool& bad) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < source.size()) {
        unsigned char c = static_cast<unsigned char>(source[i]);
        if (std::isspace(c)) { ++i; continue; }
        if (std::isdigit(c) || (c == '.' && i + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[i + 1])))) {
            std::size_t start = i;
            // No digit grouping in input: commas separate function arguments.
            while (i < source.size() && (std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.')) ++i;
            std::string digits(source.substr(start, i - start));
            if (digits.find('.') != digits.rfind('.')) { bad = true; return out; }
            out.push_back({Token::Number, std::stod(digits), {}});
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            std::size_t start = i;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
            std::string word(source.substr(start, i - start));
            for (auto& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            out.push_back({Token::Word, 0, word});
            continue;
        }
        if (std::string_view("+-*/%^(),").find(static_cast<char>(c)) != std::string_view::npos) {
            out.push_back({Token::Symbol, 0, std::string(1, static_cast<char>(c))});
            ++i;
            continue;
        }
        // Full-width operators typed with an IME read as their ASCII forms.
        static constexpr std::pair<std::string_view, char> wide[]{{"\xc3\x97", '*'}, {"\xc3\xb7", '/'}, {"\xef\xbc\x8b", '+'},
            {"\xe2\x88\x92", '-'}, {"\xef\xbc\x8a", '*'}, {"\xef\xbc\x8f", '/'}, {"\xef\xbc\x88", '('}, {"\xef\xbc\x89", ')'}};
        bool matched = false;
        for (auto [text, ascii] : wide)
            if (source.substr(i).starts_with(text)) {
                out.push_back({Token::Symbol, 0, std::string(1, ascii)});
                i += text.size();
                matched = true;
                break;
            }
        if (!matched) { bad = true; return out; }
    }
    return out;
}
struct Parser {
    std::vector<Token> const& list;
    Variables const& vars;
    std::size_t at = 0;
    Error error = Error::None;
    std::string name;
    Token const& peek() const { static Token const end{}; return at < list.size() ? list[at] : end; }
    bool symbol(char c) {
        if (peek().kind == Token::Symbol && peek().text[0] == c) { ++at; return true; }
        return false;
    }
    void fail(Error e) { if (error == Error::None) error = e; }
    double expression() {
        double value = term();
        while (error == Error::None) {
            if (symbol('+')) value += term();
            else if (symbol('-')) value -= term();
            else break;
        }
        return value;
    }
    double term() {
        double value = power();
        while (error == Error::None) {
            if (symbol('*')) value *= power();
            else if (symbol('/')) value /= power();
            else if (symbol('%')) value = std::fmod(value, power());
            else break;
        }
        return value;
    }
    double power() {
        double base = unary();
        if (error == Error::None && symbol('^')) return std::pow(base, power());
        return base;
    }
    double unary() {
        if (symbol('-')) return -unary();
        if (symbol('+')) return unary();
        return atom();
    }
    double atom() {
        auto const& token = peek();
        if (token.kind == Token::Number) {
            ++at;
            double value = token.number;
            if (peek().kind == Token::Word)
                if (auto factor = unit(peek().text)) { ++at; value *= *factor; }
            return value;
        }
        if (symbol('(')) {
            double value = expression();
            if (!symbol(')')) fail(Error::Incomplete);
            return value;
        }
        if (token.kind == Token::Word) {
            std::string word = token.text;
            ++at;
            if (auto factor = unit(word)) return *factor; // "st" alone is one stack
            std::optional<double> variable = word == "ans" ? vars.ans : word == "x" ? vars.x : word == "y" ? vars.y
                : word == "z" ? vars.z : std::nullopt;
            if (word == "ans" || word == "x" || word == "y" || word == "z") {
                if (!variable) fail(Error::Undefined);
                return variable.value_or(0);
            }
            if (word == "pi") return 3.14159265358979323846;
            return call(word);
        }
        fail(Error::Incomplete);
        return 0;
    }
    double call(std::string const& word) {
        static constexpr std::string_view known[]{"sqrt", "abs", "floor", "ceil", "round", "min", "max", "nether", "ow", "ticks", "secs"};
        bool isKnown = false;
        for (auto k : known) if (word == k) isKnown = true;
        if (!isKnown) { fail(Error::UnknownName); name = word; return 0; }
        if (!symbol('(')) { fail(Error::Incomplete); return 0; }
        std::vector<double> args{expression()};
        while (error == Error::None && symbol(',')) args.push_back(expression());
        if (!symbol(')')) { fail(Error::Incomplete); return 0; }
        double a = args[0];
        if (word == "min" || word == "max") {
            double out = a;
            for (double v : args) out = word == "min" ? std::fmin(out, v) : std::fmax(out, v);
            return out;
        }
        if (args.size() != 1) { fail(Error::Incomplete); return 0; }
        if (word == "sqrt") return std::sqrt(a);
        if (word == "abs") return std::fabs(a);
        if (word == "floor") return std::floor(a);
        if (word == "ceil") return std::ceil(a);
        if (word == "round") return std::round(a);
        // Overworld to Nether and back: 8 blocks to 1.
        if (word == "nether") return a / 8;
        if (word == "ow") return a * 8;
        // Game ticks: 20 per second.
        if (word == "ticks") return a * 20;
        return a / 20; // secs
    }
};
}

inline Result evaluate(std::string_view source, Variables const& vars) {
    bool bad = false;
    auto list = detail::tokens(source, bad);
    if (bad) return {std::nullopt, Error::Incomplete, {}};
    if (list.empty()) return {std::nullopt, Error::Empty, {}};
    detail::Parser parser{list, vars};
    double value = parser.expression();
    if (parser.error == Error::None && parser.at < list.size()) parser.fail(Error::Incomplete);
    if (parser.error != Error::None) return {std::nullopt, parser.error, parser.name};
    if (!std::isfinite(value)) return {std::nullopt, Error::Undefined, {}};
    return {value, Error::None, {}};
}
// A result as shown: thousands grouped, up to six decimals, no trailing zeros.
inline std::string format(double value) {
    double rounded = std::round(value * 1e6) / 1e6;
    if (rounded == 0) rounded = 0; // no "-0"
    bool negative = rounded < 0;
    double magnitude = std::fabs(rounded);
    double whole = std::floor(magnitude);
    auto fraction = static_cast<std::int64_t>(std::llround((magnitude - whole) * 1e6));
    if (fraction >= 1000000) { whole += 1; fraction -= 1000000; }
    std::string digits = std::to_string(static_cast<long long>(whole));
    if (whole >= 1e15) digits = std::to_string(whole).substr(0, std::to_string(whole).find('.'));
    std::string grouped;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) grouped += ',';
        grouped += digits[i];
    }
    if (fraction) {
        std::string tail = std::to_string(fraction);
        tail = std::string(6 - tail.size(), '0') + tail;
        while (!tail.empty() && tail.back() == '0') tail.pop_back();
        grouped += "." + tail;
    }
    return (negative ? "-" : "") + grouped;
}
// Whether a result is worth restating in chests and stacks (64 or more items).
inline bool restatable(double value) {
    return std::isfinite(value) && std::fabs(value) >= 64 && std::fabs(value) < 9e15 && std::floor(value) == value;
}
// Where the calculator's backspace key deletes back to: the piece a key put
// in, with the spaces around an operator (" + "), a whole function name with
// its parenthesis ("sqrt("), a whole word ("ans", "st"), else one character
// (maintainer 2026-10-13: the spaces needed presses of their own).
inline std::size_t pieceStart(std::string_view text, std::size_t caret) {
    std::size_t i = caret > text.size() ? text.size() : caret;
    auto word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i > 0 && text[i - 1] == ' ') --i;
    if (i == 0) return 0;
    char c = text[i - 1];
    if (c == '(' && i >= 2 && std::isalpha(static_cast<unsigned char>(text[i - 2]))) {
        --i;
        while (i > 0 && word(text[i - 1])) --i;
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        while (i > 0 && word(text[i - 1]) && !std::isdigit(static_cast<unsigned char>(text[i - 1]))) --i;
    } else {
        --i;
        while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) --i;
    }
    while (i > 0 && text[i - 1] == ' ') --i;
    return i;
}
// The history the panel keeps for the session, oldest first (decided
// 2026-10-13: 20 cards, newest drawn at the bottom).
struct Line {
    std::string text;
    double value = 0;
};
inline constexpr std::size_t historyLimit = 20;
inline void remember(std::vector<Line>& history, Line line) {
    if (!history.empty() && history.back().text == line.text) return;
    history.push_back(std::move(line));
    if (history.size() > historyLimit) history.erase(history.begin());
}
}
