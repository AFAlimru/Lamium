#include "ui/Calculator.h"
#include <cmath>
#include <vector>
void check(bool, char const*);
void calculatorTests() {
    using namespace lamium::ui::calculator;
    Variables none;
    auto is = [&](char const* text, double want, Variables const& vars = {}) {
        auto r = evaluate(text, vars);
        return r.value && std::fabs(*r.value - want) < 1e-9;
    };
    check(is("1 + 2 * 3", 7) && is("(1 + 2) * 3", 9) && is("10 - 4 - 3", 3) && is("2 ^ 3 ^ 2", 512) && is("-2 ^ 2", 4),
          "precedence: * before +, left to right, ^ from the right");
    check(is("7 % 3", 1) && is("1.5 * 2", 3) && is(".5 + .25", 0.75) && is("-(3 - 5)", 2), "remainder, decimals, unary minus");
    check(is("12 st + 40", 808) && is("20 lc - 1234", 67886) && is("3 sb / 7", 1728 * 3 / 7.) && is("1 ch", 1728) && is("2st", 128),
          "storage units after a number");
    check(is("st", 64) && is("ch - 1", 1727), "a unit word alone is one of it");
    check(is("nether(1520)", 190) && is("ow(-12)", -96) && is("sqrt(2)*16", std::sqrt(2.) * 16) && is("min(3, 1, 2)", 1)
              && is("max(3, 1, 2)", 3) && is("abs(-4)", 4) && is("floor(2.7)", 2) && is("ceil(2.1)", 3) && is("round(2.5)", 3),
          "functions");
    check(is("ticks(3)", 60) && is("secs(72000)", 3600), "game ticks are 20 per second");
    check(is("SQRT(16)", 4) && is("10 \xc3\x97 3", 30) && is("9 \xc3\xb7 3", 3), "words ignore case; full-width operators work");
    Variables at{100., 64., -320., -40.};
    check(is("ans * 2", 200, at) && is("(x - 100)^2 + (z + 40)^2", 25, Variables{1., 103., 0., -36.}), "variables");
    check(evaluate("x + 1", none).error == Error::Undefined, "a variable the panel cannot supply is undefined");
    check(evaluate("", none).error == Error::Empty && evaluate("   ", none).error == Error::Empty, "nothing typed");
    check(evaluate("5 *", none).error == Error::Incomplete && evaluate("(1 + 2", none).error == Error::Incomplete
              && evaluate("sqrt 4", none).error == Error::Incomplete && evaluate("1.2.3", none).error == Error::Incomplete
              && evaluate("2 3", none).error == Error::Incomplete && evaluate("#", none).error == Error::Incomplete,
          "incomplete or stray input");
    auto unknown = evaluate("foo(2)", none);
    check(unknown.error == Error::UnknownName && unknown.name == "foo", "an unknown word is named");
    check(evaluate("1 / 0", none).error == Error::Undefined && evaluate("sqrt(-1)", none).error == Error::Undefined,
          "no infinity or NaN results");
    check(format(67886) == "67,886" && format(-297.5) == "-297.5" && format(1234567.25) == "1,234,567.25" && format(0) == "0"
              && format(2.0 / 3) == "0.666667" && format(-0.0000001) == "0" && format(999) == "999",
          "results are grouped and trimmed");
    check(restatable(64) && restatable(-128) && !restatable(63) && !restatable(100.5), "only whole counts of 64 or more restate");
    auto after = [](std::string text) { return text.substr(0, pieceStart(text, text.size())); };
    check(after("20 lc - 1234") == "20 lc - 123" && after("3 + ") == "3" && after("3 +") == "3" && after("sqrt(") == ""
              && after("2 * sqrt(") == "2 *" && after("ans") == "" && after("12 st") == "12" && after("2st") == "2"
              && after("min(3, ") == "min(3" && after("   ") == "" && after("") == "",
          "the backspace key deletes what a key put in, with its spaces");
    std::vector<Line> history;
    for (int i = 0; i < 22; ++i) remember(history, {std::to_string(i), static_cast<double>(i)});
    check(history.size() == historyLimit && history.back().text == "21" && history.front().text == "2", "twenty lines, newest last");
    remember(history, {"21", 21});
    check(history.size() == historyLimit && history.back().text == "21" && history[historyLimit - 2].text == "20",
          "the same line twice in a row is kept once");
}
