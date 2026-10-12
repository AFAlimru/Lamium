#pragma once
#include "ui/TextEdit.h"
#include <string>
#include <string_view>

namespace lamium::ui {
// Native text events supply UTF-8. Keep codepoints intact when deleting and
// never truncate an event at an arbitrary byte boundary. ASCII case folding
// leaves Japanese and other UTF-8 text available for exact substring matching.
// Also the name fields' editor (any printable text up to 128 bytes).
class SearchQuery : public TextEdit {
public:
    SearchQuery() : TextEdit(128) {}
    static std::string folded(std::string_view input) {
        std::string result(input);
        for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        return result;
    }
    bool matches(std::string_view haystack) const {
        auto source = folded(haystack);
        auto query = folded(text);
        size_t begin = 0;
        while (begin < query.size()) {
            begin = query.find_first_not_of(' ', begin);
            if (begin == std::string::npos) break;
            auto end = query.find(' ', begin);
            if (end == std::string::npos) end = query.size();
            if (source.find(query.substr(begin, end - begin)) == std::string::npos) return false;
            begin = end;
        }
        return true;
    }
};
}
