#include "ui/TextFit.h"
#include <limits>
#include <utility>
void check(bool, char const*);
void textFitTests() {
    using lamium::ui::fitLabel;
    using lamium::ui::wrapLabel;
    auto measure = [](std::string_view text) {
        float width = 0;
        for (unsigned char c : text) if ((c & 0xc0) != 0x80) width += c < 128 ? 1 : 2;
        return width;
    };
    check(fitLabel("abc",3,measure) == "abc", "exact fitting labels remain unchanged");
    check(fitLabel("abcdefgh",6,measure) == "abc...", "long labels reserve ellipsis width");
    check(fitLabel("日本語設定",7,measure) == "日本...", "Japanese truncation preserves whole characters");
    check(fitLabel("A🌿日本語",6,measure) == "A🌿...", "four-byte characters survive truncation");
    check(fitLabel("abcdef",2,measure).empty(), "too little space for ellipsis draws no text");
    check(fitLabel("a",0,measure).empty() && fitLabel("a",std::numeric_limits<float>::quiet_NaN(),measure).empty(),
          "invalid label width draws no text");
    check(wrapLabel("one two three",7,3,measure) == std::vector<std::string>{"one two", "three"},
          "paragraphs wrap at word boundaries");
    check(wrapLabel("one three",6,3,measure) == std::vector<std::string>{"one", "three"},
          "words move intact to the following line");
    check(wrapLabel("日本語設定",4,3,measure) == std::vector<std::string>{"日本", "語設", "定"},
          "Japanese descriptions wrap at complete characters");
    check(wrapLabel("A🌿日本語",3,3,measure) == std::vector<std::string>{"A🌿", "日", "..."},
          "bounded paragraphs preserve emoji and truncate the final line");
    check(wrapLabel("abcdefghi",4,2,measure) == std::vector<std::string>{"abcd", "e..."},
          "unbroken text wraps and reserves final ellipsis");
    check(wrapLabel("one\ntwo\nthree",7,2,measure) == std::vector<std::string>{"one", "two ..."},
          "explicit line breaks cannot escape the final line budget");
    check(wrapLabel("日本",1,3,measure).empty() && wrapLabel("abc",4,0,measure).empty(),
          "unrenderable paragraphs terminate without partial characters");
    using lamium::ui::wrapFullText;
    using lamium::ui::textOverflows;
    check(!textOverflows("one two",7,1,measure) && textOverflows("one two three",7,1,measure)
        && textOverflows("one\ntwo\nthree",40,2,measure),
        "description expansion follows measured width, line count and explicit breaks");
    check(wrapFullText("日本語設定",4,measure) == std::vector<std::string>{"日本", "語設", "定"}
        && wrapFullText("a\n\nb",4,measure) == std::vector<std::string>{"a", "", "b"}
        && wrapFullText("🌿日本",1,measure) == std::vector<std::string>{"🌿", "日", "本"},
        "full help preserves blank lines and every complete UTF-8 codepoint without ellipsis");
    check(textOverflows("日本",1,3,measure) && !textOverflows("日本",4,1,measure)
        && textOverflows("x",0,2,measure), "oversized glyphs and invalid widths count as overflow");
}
