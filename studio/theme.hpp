// Colour themes. A theme is a table of named colours; built-in themes are defined in code and extra themes can come
// from JSON files (user themes or extensions).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "json.hpp"

namespace studio {

using Color = std::uint32_t;   // 0xRRGGBB

enum class Slot {
    // editor
    EditorBg, EditorFg, GutterBg, GutterFg, GutterFgActive, CurrentLine, Selection, Caret, IndentGuide, BracketMatch,
    Error, Warning,
    // syntax
    Keyword, Type, Builtin, String, Number, Comment, Operator, Constant,
    // chrome
    UiBg, UiFg, UiBorder, PanelBg, PanelFg, TabActiveBg, TabInactiveBg, TabFg, Accent, StatusBg, StatusFg,
    PopupBg, PopupFg, PopupSelBg, PopupSelFg,
    ConsoleBg, ConsoleFg, ConsoleInput, ConsoleError,
    Count
};

struct Theme {
    std::string name;
    bool dark = true;
    std::array<Color, static_cast<size_t>(Slot::Count)> c{};

    Color operator[](Slot s) const { return c[static_cast<size_t>(s)]; }
    Color& operator[](Slot s) { return c[static_cast<size_t>(s)]; }

    json::Value toJson() const;
    // Applies the "colors" object of v on top of this theme (unknown or malformed entries are ignored).
    // Returns how many colours were applied.
    int applyJson(const json::Value& v);
};

const char* slotName(Slot s);
bool parseHexColor(const std::string& s, Color& out);   // "#RRGGBB" or "RRGGBB"
std::string formatHexColor(Color c);

// The themes that ship with the IDE: "PCScript Dark", "PCScript Light", "Classic Blue", "High Contrast",
// "Solarized Dark", "Midnight".
const std::vector<Theme>& builtinThemes();
const Theme& defaultTheme(bool dark);
// Builds a theme from JSON: { "name": "...", "base": "dark"|"light"|<built-in theme name>, "colors": { "keyword": "#..." } }
bool themeFromJson(const json::Value& v, Theme& out, std::string* error = nullptr);

} // namespace studio
