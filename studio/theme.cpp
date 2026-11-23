#include "theme.hpp"
#include <cctype>
#include <cstdio>

namespace studio {

static const char* kSlotNames[] = {
    "editorBg", "editorFg", "gutterBg", "gutterFg", "gutterFgActive", "currentLine", "selection", "caret", "indentGuide",
    "bracketMatch", "error", "warning",
    "keyword", "type", "builtin", "string", "number", "comment", "operator", "constant",
    "uiBg", "uiFg", "uiBorder", "panelBg", "panelFg", "tabActiveBg", "tabInactiveBg", "tabFg", "accent", "statusBg",
    "statusFg", "popupBg", "popupFg", "popupSelBg", "popupSelFg",
    "consoleBg", "consoleFg", "consoleInput", "consoleError"};
static_assert(sizeof(kSlotNames) / sizeof(kSlotNames[0]) == static_cast<size_t>(Slot::Count), "slot name table out of sync");

const char* slotName(Slot s) { return kSlotNames[static_cast<size_t>(s)]; }

bool parseHexColor(const std::string& in, Color& out) {
    std::string s = in;
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() == 3) s = std::string{s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6) return false;
    Color v = 0;
    for (char ch : s) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
        v = (v << 4) | static_cast<Color>(std::isdigit(static_cast<unsigned char>(ch)) ? ch - '0' : (std::tolower(ch) - 'a' + 10));
    }
    out = v;
    return true;
}

std::string formatHexColor(Color c) {
    char b[10];
    std::snprintf(b, sizeof b, "#%06X", static_cast<unsigned>(c & 0xFFFFFF));
    return b;
}

json::Value Theme::toJson() const {
    json::Value v = json::Value::object();
    v.set("name", name);
    v.set("base", dark ? "dark" : "light");
    json::Value colors = json::Value::object();
    for (size_t i = 0; i < c.size(); ++i) colors.set(kSlotNames[i], formatHexColor(c[i]));
    v.set("colors", std::move(colors));
    return v;
}

int Theme::applyJson(const json::Value& v) {
    int n = 0;
    const json::Value& colors = v.isObject() && v.has("colors") ? v.get("colors") : v;
    if (!colors.isObject()) return 0;
    for (size_t i = 0; i < c.size(); ++i) {
        const json::Value& e = colors.get(kSlotNames[i]);
        Color col;
        if (e.isString() && parseHexColor(e.asString(), col)) { c[i] = col; ++n; }
    }
    return n;
}

namespace {
struct Row { Slot s; Color c; };

Theme make(const char* name, bool dark, std::initializer_list<Row> rows) {
    Theme t; t.name = name; t.dark = dark;
    for (const Row& r : rows) t[r.s] = r.c;
    return t;
}

std::vector<Theme> build() {
    std::vector<Theme> v;
    v.push_back(make("PCScript Dark", true, {
        {Slot::EditorBg, 0x1E1F22}, {Slot::EditorFg, 0xD7DAE0}, {Slot::GutterBg, 0x1E1F22}, {Slot::GutterFg, 0x5C6370},
        {Slot::GutterFgActive, 0xC8CCD4}, {Slot::CurrentLine, 0x26282D}, {Slot::Selection, 0x2F4F7F}, {Slot::Caret, 0xFFFFFF},
        {Slot::IndentGuide, 0x34373D}, {Slot::BracketMatch, 0x3C5A5A}, {Slot::Error, 0xF14C4C}, {Slot::Warning, 0xE5C07B},
        {Slot::Keyword, 0xC678DD}, {Slot::Type, 0x56B6C2}, {Slot::Builtin, 0x61AFEF}, {Slot::String, 0x98C379},
        {Slot::Number, 0xD19A66}, {Slot::Comment, 0x7F848E}, {Slot::Operator, 0xABB2BF}, {Slot::Constant, 0xE5C07B},
        {Slot::UiBg, 0x2B2D31}, {Slot::UiFg, 0xD7DAE0}, {Slot::UiBorder, 0x3A3D43}, {Slot::PanelBg, 0x232529},
        {Slot::PanelFg, 0xD7DAE0}, {Slot::TabActiveBg, 0x1E1F22}, {Slot::TabInactiveBg, 0x2B2D31}, {Slot::TabFg, 0xD7DAE0},
        {Slot::Accent, 0x4D8DF7}, {Slot::StatusBg, 0x3A5FA8}, {Slot::StatusFg, 0xFFFFFF}, {Slot::PopupBg, 0x2B2D31},
        {Slot::PopupFg, 0xD7DAE0}, {Slot::PopupSelBg, 0x3A5FA8}, {Slot::PopupSelFg, 0xFFFFFF}, {Slot::ConsoleBg, 0x17181A},
        {Slot::ConsoleFg, 0xD7DAE0}, {Slot::ConsoleInput, 0x7FD1FF}, {Slot::ConsoleError, 0xF48771}}));
    v.push_back(make("PCScript Light", false, {
        {Slot::EditorBg, 0xFFFFFF}, {Slot::EditorFg, 0x1F2328}, {Slot::GutterBg, 0xF6F8FA}, {Slot::GutterFg, 0x8C959F},
        {Slot::GutterFgActive, 0x1F2328}, {Slot::CurrentLine, 0xF1F5FB}, {Slot::Selection, 0xB6D4FE}, {Slot::Caret, 0x000000},
        {Slot::IndentGuide, 0xE3E7EB}, {Slot::BracketMatch, 0xD0E8D0}, {Slot::Error, 0xD1242F}, {Slot::Warning, 0xBF8700},
        {Slot::Keyword, 0x0033B3}, {Slot::Type, 0x007A7A}, {Slot::Builtin, 0x7A3E9D}, {Slot::String, 0x067D17},
        {Slot::Number, 0x1750EB}, {Slot::Comment, 0x6A737D}, {Slot::Operator, 0x444B54}, {Slot::Constant, 0x9E4A06},
        {Slot::UiBg, 0xEEF0F3}, {Slot::UiFg, 0x1F2328}, {Slot::UiBorder, 0xD0D7DE}, {Slot::PanelBg, 0xF6F8FA},
        {Slot::PanelFg, 0x1F2328}, {Slot::TabActiveBg, 0xFFFFFF}, {Slot::TabInactiveBg, 0xE4E7EB}, {Slot::TabFg, 0x1F2328},
        {Slot::Accent, 0x0969DA}, {Slot::StatusBg, 0x0969DA}, {Slot::StatusFg, 0xFFFFFF}, {Slot::PopupBg, 0xFFFFFF},
        {Slot::PopupFg, 0x1F2328}, {Slot::PopupSelBg, 0x0969DA}, {Slot::PopupSelFg, 0xFFFFFF}, {Slot::ConsoleBg, 0xFFFFFF},
        {Slot::ConsoleFg, 0x1F2328}, {Slot::ConsoleInput, 0x0550AE}, {Slot::ConsoleError, 0xCF222E}}));
    // Visual Basic 6 style: white page, blue keywords, green comments.
    v.push_back(make("Classic Blue", false, {
        {Slot::EditorBg, 0xFFFFFF}, {Slot::EditorFg, 0x000000}, {Slot::GutterBg, 0xECE9D8}, {Slot::GutterFg, 0x808080},
        {Slot::GutterFgActive, 0x000000}, {Slot::CurrentLine, 0xFFFFE1}, {Slot::Selection, 0x316AC5}, {Slot::Caret, 0x000000},
        {Slot::IndentGuide, 0xE0E0E0}, {Slot::BracketMatch, 0xC0E0C0}, {Slot::Error, 0xFF0000}, {Slot::Warning, 0xC08000},
        {Slot::Keyword, 0x0000FF}, {Slot::Type, 0x0000FF}, {Slot::Builtin, 0x000080}, {Slot::String, 0x800000},
        {Slot::Number, 0x000000}, {Slot::Comment, 0x008000}, {Slot::Operator, 0x000000}, {Slot::Constant, 0x0000FF},
        {Slot::UiBg, 0xECE9D8}, {Slot::UiFg, 0x000000}, {Slot::UiBorder, 0xACA899}, {Slot::PanelBg, 0xF5F4EA},
        {Slot::PanelFg, 0x000000}, {Slot::TabActiveBg, 0xFFFFFF}, {Slot::TabInactiveBg, 0xD6D2C2}, {Slot::TabFg, 0x000000},
        {Slot::Accent, 0x316AC5}, {Slot::StatusBg, 0x0A246A}, {Slot::StatusFg, 0xFFFFFF}, {Slot::PopupBg, 0xFFFFE1},
        {Slot::PopupFg, 0x000000}, {Slot::PopupSelBg, 0x316AC5}, {Slot::PopupSelFg, 0xFFFFFF}, {Slot::ConsoleBg, 0xFFFFFF},
        {Slot::ConsoleFg, 0x000000}, {Slot::ConsoleInput, 0x0000FF}, {Slot::ConsoleError, 0xCC0000}}));
    v.push_back(make("High Contrast", true, {
        {Slot::EditorBg, 0x000000}, {Slot::EditorFg, 0xFFFFFF}, {Slot::GutterBg, 0x000000}, {Slot::GutterFg, 0xC0C0C0},
        {Slot::GutterFgActive, 0xFFFFFF}, {Slot::CurrentLine, 0x1A1A1A}, {Slot::Selection, 0x0000AA}, {Slot::Caret, 0xFFFF00},
        {Slot::IndentGuide, 0x555555}, {Slot::BracketMatch, 0x005500}, {Slot::Error, 0xFF4040}, {Slot::Warning, 0xFFD700},
        {Slot::Keyword, 0x66CCFF}, {Slot::Type, 0x00FFD0}, {Slot::Builtin, 0xFFAA33}, {Slot::String, 0x7CFC00},
        {Slot::Number, 0xFF99FF}, {Slot::Comment, 0xA0A0A0}, {Slot::Operator, 0xFFFFFF}, {Slot::Constant, 0xFFD700},
        {Slot::UiBg, 0x000000}, {Slot::UiFg, 0xFFFFFF}, {Slot::UiBorder, 0xFFFFFF}, {Slot::PanelBg, 0x000000},
        {Slot::PanelFg, 0xFFFFFF}, {Slot::TabActiveBg, 0x000000}, {Slot::TabInactiveBg, 0x202020}, {Slot::TabFg, 0xFFFFFF},
        {Slot::Accent, 0xFFFF00}, {Slot::StatusBg, 0x0000AA}, {Slot::StatusFg, 0xFFFFFF}, {Slot::PopupBg, 0x000000},
        {Slot::PopupFg, 0xFFFFFF}, {Slot::PopupSelBg, 0xFFFF00}, {Slot::PopupSelFg, 0x000000}, {Slot::ConsoleBg, 0x000000},
        {Slot::ConsoleFg, 0xFFFFFF}, {Slot::ConsoleInput, 0x66CCFF}, {Slot::ConsoleError, 0xFF6060}}));
    v.push_back(make("Solarized Dark", true, {
        {Slot::EditorBg, 0x002B36}, {Slot::EditorFg, 0x93A1A1}, {Slot::GutterBg, 0x002B36}, {Slot::GutterFg, 0x586E75},
        {Slot::GutterFgActive, 0x93A1A1}, {Slot::CurrentLine, 0x073642}, {Slot::Selection, 0x0B5A6B}, {Slot::Caret, 0xEEE8D5},
        {Slot::IndentGuide, 0x0A3A46}, {Slot::BracketMatch, 0x2A5A4A}, {Slot::Error, 0xDC322F}, {Slot::Warning, 0xB58900},
        {Slot::Keyword, 0x859900}, {Slot::Type, 0xB58900}, {Slot::Builtin, 0x268BD2}, {Slot::String, 0x2AA198},
        {Slot::Number, 0xD33682}, {Slot::Comment, 0x586E75}, {Slot::Operator, 0x93A1A1}, {Slot::Constant, 0xCB4B16},
        {Slot::UiBg, 0x073642}, {Slot::UiFg, 0x93A1A1}, {Slot::UiBorder, 0x0F4A58}, {Slot::PanelBg, 0x00252E},
        {Slot::PanelFg, 0x93A1A1}, {Slot::TabActiveBg, 0x002B36}, {Slot::TabInactiveBg, 0x073642}, {Slot::TabFg, 0x93A1A1},
        {Slot::Accent, 0x268BD2}, {Slot::StatusBg, 0x268BD2}, {Slot::StatusFg, 0x002B36}, {Slot::PopupBg, 0x073642},
        {Slot::PopupFg, 0x93A1A1}, {Slot::PopupSelBg, 0x268BD2}, {Slot::PopupSelFg, 0x002B36}, {Slot::ConsoleBg, 0x00212A},
        {Slot::ConsoleFg, 0x93A1A1}, {Slot::ConsoleInput, 0x2AA198}, {Slot::ConsoleError, 0xDC322F}}));
    v.push_back(make("Midnight", true, {
        {Slot::EditorBg, 0x0D1117}, {Slot::EditorFg, 0xC9D1D9}, {Slot::GutterBg, 0x0D1117}, {Slot::GutterFg, 0x484F58},
        {Slot::GutterFgActive, 0xC9D1D9}, {Slot::CurrentLine, 0x161B22}, {Slot::Selection, 0x1F3A5F}, {Slot::Caret, 0x58A6FF},
        {Slot::IndentGuide, 0x21262D}, {Slot::BracketMatch, 0x264F3A}, {Slot::Error, 0xF85149}, {Slot::Warning, 0xD29922},
        {Slot::Keyword, 0xFF7B72}, {Slot::Type, 0x79C0FF}, {Slot::Builtin, 0xD2A8FF}, {Slot::String, 0xA5D6FF},
        {Slot::Number, 0x79C0FF}, {Slot::Comment, 0x8B949E}, {Slot::Operator, 0xC9D1D9}, {Slot::Constant, 0xFFA657},
        {Slot::UiBg, 0x161B22}, {Slot::UiFg, 0xC9D1D9}, {Slot::UiBorder, 0x30363D}, {Slot::PanelBg, 0x0D1117},
        {Slot::PanelFg, 0xC9D1D9}, {Slot::TabActiveBg, 0x0D1117}, {Slot::TabInactiveBg, 0x161B22}, {Slot::TabFg, 0xC9D1D9},
        {Slot::Accent, 0x58A6FF}, {Slot::StatusBg, 0x1F6FEB}, {Slot::StatusFg, 0xFFFFFF}, {Slot::PopupBg, 0x161B22},
        {Slot::PopupFg, 0xC9D1D9}, {Slot::PopupSelBg, 0x1F6FEB}, {Slot::PopupSelFg, 0xFFFFFF}, {Slot::ConsoleBg, 0x010409},
        {Slot::ConsoleFg, 0xC9D1D9}, {Slot::ConsoleInput, 0x79C0FF}, {Slot::ConsoleError, 0xFF7B72}}));
    return v;
}
} // namespace

const std::vector<Theme>& builtinThemes() { static const std::vector<Theme> t = build(); return t; }
const Theme& defaultTheme(bool dark) { return builtinThemes()[dark ? 0 : 1]; }

bool themeFromJson(const json::Value& v, Theme& out, std::string* error) {
    if (!v.isObject()) { if (error) *error = "theme must be a JSON object"; return false; }
    std::string name = v.get("name").asString();
    if (name.empty()) { if (error) *error = "theme has no \"name\""; return false; }
    std::string base = v.get("base").asString("dark");
    Theme t = defaultTheme(base != "light");
    for (const Theme& b : builtinThemes()) if (b.name == base) t = b;
    t.name = name;
    if (v.get("dark").isBool()) t.dark = v.get("dark").asBool();
    else if (base == "light") t.dark = false;
    if (v.has("colors") && !v.get("colors").isObject()) { if (error) *error = "\"colors\" must be an object"; return false; }
    t.applyJson(v);
    out = std::move(t);
    return true;
}

} // namespace studio
