// Small Win32 helpers shared by the GUI sources (UTF-8 <-> UTF-16, fonts, DPI, common dialogs, clipboard).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "theme.hpp"

namespace win {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& wide);
inline COLORREF rgb(studio::Color c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

int dpiOf(HWND hwnd);                                   // 96 when unknown
inline int scale(int px, int dpi) { return MulDiv(px, dpi, 96); }
HFONT makeFont(const std::string& family, int points, int dpi, bool bold = false);
HFONT makeUiFont(int dpi, bool bold = false);           // Segoe UI 9 pt

std::wstring windowText(HWND h);
void setWindowText(HWND h, const std::string& utf8);
std::string windowTextUtf8(HWND h);

// Standard dialogs. All paths are UTF-8.
bool openFileDialog(HWND owner, const std::string& initialDir, std::string& pathOut);
bool saveFileDialog(HWND owner, const std::string& initialDir, const std::string& suggestedName, std::string& pathOut);
bool pickFolder(HWND owner, const std::string& title, std::string& pathOut);

void info(HWND owner, const std::string& title, const std::string& text);
void warn(HWND owner, const std::string& title, const std::string& text);
bool confirm(HWND owner, const std::string& title, const std::string& text);
// Yes = save, No = discard, Cancel = abort
int askSaveChanges(HWND owner, const std::string& fileName);

bool clipboardSet(HWND owner, const std::string& utf8);
std::string clipboardGet(HWND owner);

// Known folders (UTF-8). Empty string on failure.
std::string knownFolderDocuments();
std::string knownFolderAppData();
std::string moduleDirectory();                          // folder containing the running .exe
void shellOpen(HWND owner, const std::string& pathOrUrl);
void revealInExplorer(HWND owner, const std::string& path);
bool isDarkSystem();

// Applies "DarkMode_Explorer" (dark scrollbars etc.) or "Explorer" to a control and the dark title bar to a top-level window.
void themeControl(HWND h, bool dark);
void themeTitleBar(HWND top, bool dark);

} // namespace win
