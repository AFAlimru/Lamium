#pragma once
#include <string>

namespace lamium {
// Hands a web link to the system's browser; false when it could not.
bool openUrl(std::string const& url);
// Puts text on the system clipboard; false when it could not.
bool copyText(std::string const& text);
// The clipboard's text as UTF-8; empty when there is none.
std::string clipboardText();
}
