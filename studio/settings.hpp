// User-editable IDE settings. Every value is validated/clamped when loaded, so a hand-edited settings.json can never
// put the IDE into an unusable state.
#pragma once
#include <string>
#include "json.hpp"

namespace studio {

struct Settings {
    // ---- Editor appearance ----
    std::string fontFamily = "Consolas";
    int fontSize = 14;              // points, 8..48
    int lineSpacing = 135;          // line height as a percentage of the font height, 100..250
    bool showLineNumbers = true;
    bool highlightCurrentLine = true;
    bool showIndentGuides = true;
    bool showWhitespace = false;
    bool matchBrackets = true;
    std::string caretStyle = "line";   // "line" | "block" | "underline"

    // ---- Typing behaviour ----
    int tabSize = 4;                // 1..8
    bool insertSpaces = true;       // Tab key inserts spaces (true) or a tab character (false)
    bool autoIndent = true;
    bool autoCloseBrackets = true;  // ( [ " '
    bool autoCloseBlocks = true;    // Enter after IF ... THEN adds ENDIF, etc.
    bool autoCapitalizeKeywords = true;   // typing "endif " becomes "ENDIF "
    bool autoComplete = true;       // the suggestion drop-down
    int autoCompleteMinChars = 1;   // characters typed before the drop-down opens on its own, 1..5

    // ---- Files ----
    bool formatOnSave = false;
    bool trimTrailingWhitespace = true;
    bool insertFinalNewline = true;
    int autoSaveSeconds = 0;        // 0 = off, otherwise 5..600
    bool restoreSession = true;

    // ---- Running programs ----
    bool saveBeforeRun = true;
    bool clearConsoleOnRun = true;
    int timeoutSeconds = 0;         // 0 = no limit, else 1..3600
    int maxCallDepth = 5000;        // 100..20000
    bool checkWhileTyping = true;   // live error squiggles / Problems list

    // ---- Interface ----
    std::string theme = "PCScript Dark";
    std::string consoleFontFamily = "Consolas";
    int consoleFontSize = 13;       // 8..40
    bool showExplorer = true;
    bool showConsole = true;
    bool showStatusBar = true;
    bool showToolbar = true;

    json::Value toJson() const;
    // Reads known keys from v; missing or invalid keys keep the current value. Returns the number of keys that were
    // out of range and had to be corrected.
    int fromJson(const json::Value& v);
    void clamp();
    bool operator==(const Settings& o) const { return json::dump(toJson(), -1) == json::dump(o.toJson(), -1); }
};

} // namespace studio
