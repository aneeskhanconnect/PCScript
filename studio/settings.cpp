#include "settings.hpp"
#include <algorithm>

namespace studio {

static int clampInt(int v, int lo, int hi, int& fixes) {
    int r = std::min(std::max(v, lo), hi);
    if (r != v) ++fixes;
    return r;
}

void Settings::clamp() {
    int f = 0;
    fontSize = clampInt(fontSize, 8, 48, f);
    lineSpacing = clampInt(lineSpacing, 100, 250, f);
    tabSize = clampInt(tabSize, 1, 8, f);
    autoCompleteMinChars = clampInt(autoCompleteMinChars, 1, 5, f);
    if (autoSaveSeconds != 0) autoSaveSeconds = clampInt(autoSaveSeconds, 5, 600, f);
    if (timeoutSeconds != 0) timeoutSeconds = clampInt(timeoutSeconds, 1, 3600, f);
    maxCallDepth = clampInt(maxCallDepth, 100, 20000, f);
    consoleFontSize = clampInt(consoleFontSize, 8, 40, f);
    if (caretStyle != "line" && caretStyle != "block" && caretStyle != "underline") caretStyle = "line";
    if (fontFamily.empty()) fontFamily = "Consolas";
    if (consoleFontFamily.empty()) consoleFontFamily = "Consolas";
    if (theme.empty()) theme = "PCScript Dark";
}

json::Value Settings::toJson() const {
    json::Value v = json::Value::object();
    v.set("fontFamily", fontFamily); v.set("fontSize", fontSize); v.set("lineSpacing", lineSpacing);
    v.set("showLineNumbers", showLineNumbers); v.set("highlightCurrentLine", highlightCurrentLine);
    v.set("showIndentGuides", showIndentGuides); v.set("showWhitespace", showWhitespace);
    v.set("matchBrackets", matchBrackets); v.set("caretStyle", caretStyle);
    v.set("tabSize", tabSize); v.set("insertSpaces", insertSpaces); v.set("autoIndent", autoIndent);
    v.set("autoCloseBrackets", autoCloseBrackets); v.set("autoCloseBlocks", autoCloseBlocks);
    v.set("autoCapitalizeKeywords", autoCapitalizeKeywords); v.set("autoComplete", autoComplete);
    v.set("autoCompleteMinChars", autoCompleteMinChars);
    v.set("formatOnSave", formatOnSave); v.set("trimTrailingWhitespace", trimTrailingWhitespace);
    v.set("insertFinalNewline", insertFinalNewline); v.set("autoSaveSeconds", autoSaveSeconds);
    v.set("restoreSession", restoreSession);
    v.set("saveBeforeRun", saveBeforeRun); v.set("clearConsoleOnRun", clearConsoleOnRun);
    v.set("timeoutSeconds", timeoutSeconds); v.set("maxCallDepth", maxCallDepth);
    v.set("checkWhileTyping", checkWhileTyping);
    v.set("theme", theme); v.set("consoleFontFamily", consoleFontFamily); v.set("consoleFontSize", consoleFontSize);
    v.set("showExplorer", showExplorer); v.set("showConsole", showConsole);
    v.set("showStatusBar", showStatusBar); v.set("showToolbar", showToolbar);
    return v;
}

int Settings::fromJson(const json::Value& v) {
    if (!v.isObject()) return 0;
    auto b = [&](const char* k, bool& dst) { if (v.get(k).isBool()) dst = v.get(k).asBool(); };
    auto n = [&](const char* k, int& dst) { if (v.get(k).isNumber()) dst = v.get(k).asInt(); };
    auto s = [&](const char* k, std::string& dst) { if (v.get(k).isString()) dst = v.get(k).asString(); };
    s("fontFamily", fontFamily); n("fontSize", fontSize); n("lineSpacing", lineSpacing);
    b("showLineNumbers", showLineNumbers); b("highlightCurrentLine", highlightCurrentLine);
    b("showIndentGuides", showIndentGuides); b("showWhitespace", showWhitespace);
    b("matchBrackets", matchBrackets); s("caretStyle", caretStyle);
    n("tabSize", tabSize); b("insertSpaces", insertSpaces); b("autoIndent", autoIndent);
    b("autoCloseBrackets", autoCloseBrackets); b("autoCloseBlocks", autoCloseBlocks);
    b("autoCapitalizeKeywords", autoCapitalizeKeywords); b("autoComplete", autoComplete);
    n("autoCompleteMinChars", autoCompleteMinChars);
    b("formatOnSave", formatOnSave); b("trimTrailingWhitespace", trimTrailingWhitespace);
    b("insertFinalNewline", insertFinalNewline); n("autoSaveSeconds", autoSaveSeconds);
    b("restoreSession", restoreSession);
    b("saveBeforeRun", saveBeforeRun); b("clearConsoleOnRun", clearConsoleOnRun);
    n("timeoutSeconds", timeoutSeconds); n("maxCallDepth", maxCallDepth);
    b("checkWhileTyping", checkWhileTyping);
    s("theme", theme); s("consoleFontFamily", consoleFontFamily); n("consoleFontSize", consoleFontSize);
    b("showExplorer", showExplorer); b("showConsole", showConsole);
    b("showStatusBar", showStatusBar); b("showToolbar", showToolbar);
    Settings before = *this;
    clamp();
    int fixes = 0;
    if (fontSize != before.fontSize) ++fixes;
    if (lineSpacing != before.lineSpacing) ++fixes;
    if (tabSize != before.tabSize) ++fixes;
    if (maxCallDepth != before.maxCallDepth) ++fixes;
    if (consoleFontSize != before.consoleFontSize) ++fixes;
    if (autoCompleteMinChars != before.autoCompleteMinChars) ++fixes;
    if (autoSaveSeconds != before.autoSaveSeconds) ++fixes;
    if (timeoutSeconds != before.timeoutSeconds) ++fixes;
    if (caretStyle != before.caretStyle) ++fixes;
    return fixes;
}

} // namespace studio
