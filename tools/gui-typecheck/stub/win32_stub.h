// Hand-written stub of the Win32 API subset used by PCScript Studio. FOR SYNTAX CHECKING ONLY (g++ -fsyntax-only).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#define WINAPI
#define CALLBACK
#define APIENTRY
#define CONST const
#define MAX_PATH 260
#define LF_FACESIZE 32
#define CP_UTF8 65001
#define SUCCEEDED(hr) ((hr) >= 0)
#define TRUE 1
#define FALSE 0
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD; typedef unsigned long DWORD; typedef unsigned int UINT;
typedef int errno_t; typedef int INT; typedef long LONG; typedef short SHORT; typedef unsigned short USHORT; typedef unsigned long ULONG;
typedef intptr_t INT_PTR; typedef uintptr_t UINT_PTR; typedef intptr_t LONG_PTR; typedef uintptr_t ULONG_PTR; typedef uintptr_t DWORD_PTR;
typedef unsigned long long ULONGLONG;
typedef UINT_PTR WPARAM; typedef LONG_PTR LPARAM; typedef LONG_PTR LRESULT; typedef DWORD COLORREF; typedef long HRESULT;
typedef wchar_t WCHAR; typedef WCHAR* LPWSTR; typedef const WCHAR* LPCWSTR; typedef WCHAR* PWSTR; typedef const WCHAR* PCWSTR;
typedef void* LPVOID; typedef const void* LPCVOID; typedef void* PVOID; typedef void* HANDLE;
#define DECLARE_HANDLE(n) struct n##__; typedef n##__* n
DECLARE_HANDLE(HWND); DECLARE_HANDLE(HDC); DECLARE_HANDLE(HINSTANCE); DECLARE_HANDLE(HICON); DECLARE_HANDLE(HBRUSH); DECLARE_HANDLE(HFONT);
DECLARE_HANDLE(HPEN); DECLARE_HANDLE(HBITMAP); DECLARE_HANDLE(HMENU); DECLARE_HANDLE(HACCEL); DECLARE_HANDLE(HDROP); DECLARE_HANDLE(HKEY);
DECLARE_HANDLE(HMONITOR); DECLARE_HANDLE(HRGN);
typedef void* HGDIOBJ; typedef void* HGLOBAL;
typedef HINSTANCE HMODULE; typedef HICON HCURSOR;
struct _TREEITEM; typedef _TREEITEM* HTREEITEM;
typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef LRESULT (*SUBCLASSPROC)(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
typedef int (*FONTENUMPROCW)(const struct tagLOGFONTW*, const struct tagTEXTMETRICW*, DWORD, LPARAM);
typedef UINT_PTR (*TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

struct POINT { LONG x, y; }; struct SIZE { LONG cx, cy; }; struct RECT { LONG left, top, right, bottom; };
typedef const RECT* LPCRECT; typedef RECT* LPRECT;
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
struct WNDCLASSEXW { UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra; int cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm; };
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore; BOOL fIncUpdate; BYTE rgbReserved[32]; };
struct tagLOGFONTW { LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight; BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily; WCHAR lfFaceName[LF_FACESIZE]; };
typedef tagLOGFONTW LOGFONTW;
struct tagTEXTMETRICW { LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY; WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar; BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet; };
typedef tagTEXTMETRICW TEXTMETRICW;
struct SCROLLINFO { UINT cbSize; UINT fMask; int nMin, nMax; UINT nPage; int nPos; int nTrackPos; };
struct CREATESTRUCTW { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy, cx, y, x; LONG style; LPCWSTR lpszName; LPCWSTR lpszClass; DWORD dwExStyle; };
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct MONITORINFO { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; };
struct WINDOWPLACEMENT { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; RECT rcDevice; };
struct TRACKMOUSEEVENT { DWORD cbSize, dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct ACCEL { BYTE fVirt; WORD key; WORD cmd; };
struct NMHDR { HWND hwndFrom; UINT_PTR idFrom; UINT code; };
struct INITCOMMONCONTROLSEX { DWORD dwSize, dwICC; };
struct OPENFILENAMEW { DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter; DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle; LPCWSTR lpstrInitialDir; LPCWSTR lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension; LPCWSTR lpstrDefExt; LPARAM lCustData; LPVOID lpfnHook; LPCWSTR lpTemplateName; };
typedef void* PIDLIST_ABSOLUTE;
struct BROWSEINFOW { HWND hwndOwner; PIDLIST_ABSOLUTE pidlRoot; LPWSTR pszDisplayName; LPCWSTR lpszTitle; UINT ulFlags; LPVOID lpfn; LPARAM lParam; int iImage; };
struct GUID { unsigned long Data1; unsigned short Data2, Data3; unsigned char Data4[8]; };
typedef const GUID& REFKNOWNFOLDERID;
extern const GUID FOLDERID_Documents; extern const GUID FOLDERID_RoamingAppData;
struct TCITEMW { UINT mask; DWORD dwState, dwStateMask; LPWSTR pszText; int cchTextMax; int iImage; LPARAM lParam; };
struct LVCOLUMNW { UINT mask; int fmt, cx; LPWSTR pszText; int cchTextMax, iSubItem, iImage, iOrder; };
struct LVITEMW { UINT mask; int iItem, iSubItem; UINT state, stateMask; LPWSTR pszText; int cchTextMax, iImage; LPARAM lParam; int iIndent; };
struct NMLISTVIEW { NMHDR hdr; int iItem, iSubItem; UINT uNewState, uOldState, uChanged; POINT ptAction; LPARAM lParam; };
struct TVITEMW { UINT mask; HTREEITEM hItem; UINT state, stateMask; LPWSTR pszText; int cchTextMax, iImage, iSelectedImage, cChildren; LPARAM lParam; };
struct TVINSERTSTRUCTW { HTREEITEM hParent; HTREEITEM hInsertAfter; TVITEMW item; };
struct NMTREEVIEWW { NMHDR hdr; UINT action; TVITEMW itemOld, itemNew; POINT ptDrag; };
struct NMTVKEYDOWN { NMHDR hdr; WORD wVKey; UINT flags; };
struct TVHITTESTINFO { POINT pt; UINT flags; HTREEITEM hItem; };
struct CHARRANGE { LONG cpMin, cpMax; };
struct CHARFORMAT2W { UINT cbSize; DWORD dwMask, dwEffects; LONG yHeight, yOffset; COLORREF crTextColor; BYTE bCharSet, bPitchAndFamily; WCHAR szFaceName[LF_FACESIZE]; WORD wWeight; SHORT sSpacing; COLORREF crBackColor; };
struct GETTEXTLENGTHEX { DWORD flags; UINT codepage; };

// constants
#define CW_USEDEFAULT ((int)0x80000000)
#define HWND_TOPMOST ((HWND)-1)
#define TVI_ROOT ((HTREEITEM)-0x10000)
#define TVI_LAST ((HTREEITEM)-0xFFFE)
#define HTCLIENT 1
#define SW_HIDE 0
#define SW_SHOW 5
#define SW_SHOWNORMAL 1
#define SW_SHOWMAXIMIZED 3
#define SW_SHOWNOACTIVATE 4
#define SWP_NOSIZE 1
#define SWP_NOMOVE 2
#define SWP_NOZORDER 4
#define SWP_NOACTIVATE 0x10
#define SWP_SHOWWINDOW 0x40
#define SIZE_MINIMIZED 1
#define GWLP_USERDATA (-21)
#define GA_ROOT 2
#define MA_NOACTIVATE 3
#define COLOR_BTNFACE 15
#define DC_BRUSH 18
#define SRCCOPY 0x00CC0020
#define TRANSPARENT 1
#define DEFAULT_CHARSET 1
#define CLEARTYPE_QUALITY 5
#define FIXED_PITCH 1
#define FF_MODERN 0x30
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define PS_SOLID 0
#define MONITOR_DEFAULTTONEAREST 2
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define RDW_INVALIDATE 1
#define RDW_ALLCHILDREN 0x80
#define RDW_UPDATENOW 0x100
#define PM_REMOVE 1
#define TME_LEAVE 2
#define CS_DBLCLKS 8
#define _TRUNCATE ((size_t)-1)
#define _countof(a) (sizeof(a)/sizeof(a[0]))
#define MAKEWPARAM(l,h) ((WPARAM)(((WORD)(l))|((DWORD)((WORD)(h)))<<16))
#define LOWORD(l) ((WORD)(((DWORD_PTR)(l)) & 0xffff))
#define HIWORD(l) ((WORD)((((DWORD_PTR)(l)) >> 16) & 0xffff))
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r)|((WORD)((BYTE)(g))<<8))|(((DWORD)(BYTE)(b))<<16)))
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#define IDC_ARROW MAKEINTRESOURCEW(32512)
#define IDC_IBEAM MAKEINTRESOURCEW(32513)
#define IDC_WAIT MAKEINTRESOURCEW(32514)
#define IDC_SIZEWE MAKEINTRESOURCEW(32644)
#define IDC_SIZENS MAKEINTRESOURCEW(32645)
#define IDC_HAND MAKEINTRESOURCEW(32649)
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define MB_OK 0
#define MB_YESNO 4
#define MB_YESNOCANCEL 3
#define MB_ICONINFORMATION 0x40
#define MB_ICONWARNING 0x30
#define MB_ICONQUESTION 0x20
#define COINIT_APARTMENTTHREADED 2
#define GMEM_MOVEABLE 2
#define CF_UNICODETEXT 13
#define ERROR_SUCCESS 0
#define RRF_RT_REG_DWORD 0x10
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)0x80000001)
#define INFINITE 0xFFFFFFFF
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x10000
#define BIF_RETURNONLYFSDIRS 1
#define BIF_NEWDIALOGSTYLE 0x40
#define BIF_USENEWUI 0x50
#define OFN_FILEMUSTEXIST 0x1000
#define OFN_PATHMUSTEXIST 0x800
#define OFN_EXPLORER 0x80000
#define OFN_NOCHANGEDIR 8
#define OFN_OVERWRITEPROMPT 2
// window styles
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define WS_BORDER 0x00800000L
#define WS_VSCROLL 0x00200000L
#define WS_HSCROLL 0x00100000L
#define WS_TABSTOP 0x00010000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_EX_TOOLWINDOW 0x80
#define WS_EX_NOACTIVATE 0x08000000L
#define WS_EX_TOPMOST 8
#define WS_EX_CLIENTEDGE 0x200
#define WS_EX_DLGMODALFRAME 1
#define WS_EX_CONTROLPARENT 0x10000
#define WS_EX_APPWINDOW 0x40000
#define WS_EX_ACCEPTFILES 0x10
#define SS_LEFT 0
#define SS_NOPREFIX 0x80
#define ES_AUTOHSCROLL 0x80
#define ES_MULTILINE 4
#define ES_READONLY 0x800
#define ES_AUTOVSCROLL 0x40
#define ES_NUMBER 0x2000
#define ES_PASSWORD 0x20
#define BS_PUSHBUTTON 0
#define BS_DEFPUSHBUTTON 1
#define BS_AUTOCHECKBOX 3
#define BM_GETCHECK 0xF0
#define BM_SETCHECK 0xF1
#define BST_UNCHECKED 0
#define BST_CHECKED 1
#define CBS_DROPDOWN 2
#define CBS_DROPDOWNLIST 3
#define CBS_AUTOHSCROLL 0x40
#define CB_ADDSTRING 0x143
#define CB_RESETCONTENT 0x14B
#define CB_SETCURSEL 0x14E
#define CB_GETCURSEL 0x147
#define CB_GETLBTEXT 0x148
#define LBS_NOTIFY 1
#define LB_ADDSTRING 0x180
#define LB_FINDSTRINGEXACT 0x1A2
#define LB_SETCURSEL 0x186
#define LB_GETCURSEL 0x188
#define LBN_SELCHANGE 1
#define DT_LEFT 0
#define DT_CENTER 1
#define DT_RIGHT 2
#define DT_VCENTER 4
#define DT_SINGLELINE 0x20
#define DT_NOPREFIX 0x800
#define DT_END_ELLIPSIS 0x8000
#define DLGC_WANTALLKEYS 4
#define DLGC_WANTCHARS 0x80
#define DLGC_WANTARROWS 1
#define SB_VERT 1
#define SB_HORZ 0
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_BOTTOM 7
#define SIF_RANGE 1
#define SIF_PAGE 2
#define SIF_POS 4
#define SIF_TRACKPOS 0x10
#define SIF_ALL 0x17
#define MK_CONTROL 8
#define MK_SHIFT 4
#define UNICODE_NOCHAR 0xFFFF
#define FVIRTKEY 1
#define FSHIFT 4
#define FCONTROL 8
#define FALT 0x10
#define MF_STRING 0
#define MF_GRAYED 1
#define MF_POPUP 0x10
#define MF_SEPARATOR 0x800
#define MF_BYPOSITION 0x400
#define MF_BYCOMMAND 0
#define MF_ENABLED 0
#define MF_CHECKED 8
#define MF_UNCHECKED 0
#define TPM_RIGHTBUTTON 2
#define TPM_LEFTALIGN 0
// virtual keys
#define VK_BACK 8
#define VK_TAB 9
#define VK_RETURN 13
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 27
#define VK_SPACE 32
#define VK_PRIOR 33
#define VK_NEXT 34
#define VK_END 35
#define VK_HOME 36
#define VK_LEFT 37
#define VK_UP 38
#define VK_RIGHT 39
#define VK_DOWN 40
#define VK_INSERT 45
#define VK_DELETE 46
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_F1 0x70
#define VK_F3 0x72
#define VK_F5 0x74
#define VK_F7 0x76
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_6 0xDD
// messages
#define WM_CREATE 1
#define WM_DESTROY 2
#define WM_SIZE 5
#define WM_SETFOCUS 7
#define WM_KILLFOCUS 8
#define WM_PAINT 0xF
#define WM_CLOSE 0x10
#define WM_ERASEBKGND 0x14
#define WM_SETCURSOR 0x20
#define WM_MOUSEACTIVATE 0x21
#define WM_GETMINMAXINFO 0x24
#define WM_SETFONT 0x30
#define WM_NCCREATE 0x81
#define WM_NCDESTROY 0x82
#define WM_GETDLGCODE 0x87
#define WM_KEYDOWN 0x100
#define WM_CHAR 0x102
#define WM_SYSKEYDOWN 0x104
#define WM_UNICHAR 0x109
#define WM_COMMAND 0x111
#define WM_TIMER 0x113
#define WM_HSCROLL 0x114
#define WM_VSCROLL 0x115
#define WM_INITMENUPOPUP 0x117
#define WM_CTLCOLOREDIT 0x133
#define WM_MOUSEMOVE 0x200
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONUP 0x202
#define WM_LBUTTONDBLCLK 0x203
#define WM_MBUTTONUP 0x208
#define WM_MOUSEWHEEL 0x20A
#define WM_CAPTURECHANGED 0x215
#define WM_MOUSELEAVE 0x2A3
#define WM_CONTEXTMENU 0x7B
#define WM_NOTIFY 0x4E
#define WM_DROPFILES 0x233
#define WM_DPICHANGED 0x2E0
#define WM_DPICHANGED_AFTERPARENT 0x2E3
#define WM_CUT 0x300
#define WM_COPY 0x301
#define WM_PASTE 0x302
#define WM_APP 0x8000
// edit/rich edit
#define EM_SETSEL 0xB1
#define EM_SETREADONLY 0xCF
#define EM_REPLACESEL 0xC2
#define EM_EXSETSEL (WM_APP-0x8000+0x400+55)
#define EM_EXLIMITTEXT (0x400+53)
#define EM_SETBKGNDCOLOR (0x400+67)
#define EM_SETCHARFORMAT (0x400+68)
#define EM_GETTEXTLENGTHEX (0x400+95)
#define SCF_SELECTION 1
#define SCF_DEFAULT 0
#define SCF_ALL 4
#define CFM_COLOR 0x40000000
#define CFM_EFFECTS 0x80000000
#define CFM_FACE 0x20000000
#define CFM_SIZE 0x80000000
#define GTL_DEFAULT 0
#define NM_DBLCLK ((UINT)-3)
#define NM_RCLICK ((UINT)-5)
// functions
extern "C++" {
HINSTANCE GetModuleHandleW(LPCWSTR); HMODULE LoadLibraryW(LPCWSTR); DWORD GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL DestroyWindow(HWND); BOOL IsWindow(HWND); BOOL IsWindowVisible(HWND); BOOL ShowWindow(HWND, int); BOOL UpdateWindow(HWND);
BOOL MoveWindow(HWND, int, int, int, int, BOOL); BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
BOOL PtInRect(const RECT*, POINT); BOOL GetClientRect(HWND, LPRECT); BOOL GetWindowRect(HWND, LPRECT); BOOL ClientToScreen(HWND, POINT*); BOOL ScreenToClient(HWND, POINT*);
BOOL InvalidateRect(HWND, const RECT*, BOOL); BOOL RedrawWindow(HWND, const RECT*, HRGN, UINT);
HWND GetParent(HWND); HWND GetAncestor(HWND, UINT); HWND GetDlgItem(HWND, int); HWND GetFocus(); HWND SetFocus(HWND); HWND GetCapture(); HWND SetCapture(HWND); BOOL ReleaseCapture();
BOOL EnableWindow(HWND, BOOL); BOOL SetForegroundWindow(HWND); HWND SetActiveWindow(HWND);
LRESULT SendMessageW(HWND, UINT, WPARAM, LPARAM); BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM); LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL GetMessageW(MSG*, HWND, UINT, UINT); BOOL PeekMessageW(MSG*, HWND, UINT, UINT, UINT); BOOL TranslateMessage(const MSG*); LRESULT DispatchMessageW(const MSG*);
BOOL IsDialogMessageW(HWND, MSG*); int TranslateAcceleratorW(HWND, HACCEL, MSG*); void PostQuitMessage(int);
BOOL GetClassInfoExW(HINSTANCE, LPCWSTR, WNDCLASSEXW*); UINT RegisterClassExW(const WNDCLASSEXW*);
LONG_PTR GetWindowLongPtrW(HWND, int); LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
HICON LoadIconW(HINSTANCE, LPCWSTR); HCURSOR LoadCursorW(HINSTANCE, LPCWSTR); HCURSOR SetCursor(HCURSOR); BOOL GetCursorPos(POINT*);
UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC); BOOL KillTimer(HWND, UINT_PTR); DWORD GetTickCount(); ULONGLONG GetTickCount64(); void Sleep(DWORD); DWORD GetMessageTime();
short GetKeyState(int); UINT GetDoubleClickTime(); BOOL TrackMouseEvent(TRACKMOUSEEVENT*);
HDC BeginPaint(HWND, PAINTSTRUCT*); BOOL EndPaint(HWND, const PAINTSTRUCT*); HDC GetDC(HWND); int ReleaseDC(HWND, HDC);
HDC CreateCompatibleDC(HDC); BOOL DeleteDC(HDC); HBITMAP CreateCompatibleBitmap(HDC, int, int); HGDIOBJ SelectObject(HDC, HGDIOBJ); BOOL DeleteObject(HGDIOBJ);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD); int GetDeviceCaps(HDC, int); HGDIOBJ GetStockObject(int);
COLORREF SetDCBrushColor(HDC, COLORREF); int FillRect(HDC, const RECT*, HBRUSH); COLORREF SetTextColor(HDC, COLORREF); COLORREF SetBkColor(HDC, COLORREF); int SetBkMode(HDC, int);
int DrawTextW(HDC, LPCWSTR, int, LPRECT, UINT); BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE*); BOOL GetTextMetricsW(HDC, TEXTMETRICW*);
BOOL ExtTextOutW(HDC, int, int, UINT, const RECT*, LPCWSTR, UINT, const INT*);
HFONT CreateFontIndirectW(const LOGFONTW*); HPEN CreatePen(int, int, COLORREF); BOOL MoveToEx(HDC, int, int, POINT*); BOOL LineTo(HDC, int, int);
int SaveDC(HDC); BOOL RestoreDC(HDC, int); int IntersectClipRect(HDC, int, int, int, int);
int MulDiv(int, int, int); UINT GetDpiForWindow(HWND); BOOL AdjustWindowRectExForDpi(LPRECT, DWORD, BOOL, DWORD, UINT);
int GetSystemMetrics(int); HMONITOR MonitorFromPoint(POINT, DWORD); BOOL GetMonitorInfoW(HMONITOR, MONITORINFO*);
int SetScrollInfo(HWND, int, const SCROLLINFO*, BOOL); BOOL GetScrollInfo(HWND, int, SCROLLINFO*);
BOOL CreateCaret(HWND, HBITMAP, int, int); BOOL DestroyCaret(); BOOL ShowCaret(HWND); BOOL SetCaretPos(int, int);
HMENU CreateMenu(); HMENU CreatePopupMenu(); BOOL AppendMenuW(HMENU, UINT, UINT_PTR, LPCWSTR); BOOL DeleteMenu(HMENU, UINT, UINT); int GetMenuItemCount(HMENU);
BOOL SetMenu(HWND, HMENU); BOOL DestroyMenu(HMENU); BOOL TrackPopupMenu(HMENU, UINT, int, int, int, HWND, const RECT*); HMENU GetSubMenu(HMENU, int);
DWORD EnableMenuItem(HMENU, UINT, UINT); DWORD CheckMenuItem(HMENU, UINT, UINT); BOOL ModifyMenuW(HMENU, UINT, UINT, UINT_PTR, LPCWSTR);
HACCEL CreateAcceleratorTableW(ACCEL*, int); BOOL DestroyAcceleratorTable(HACCEL);
int GetWindowTextLengthW(HWND); int GetWindowTextW(HWND, LPWSTR, int); BOOL SetWindowTextW(HWND, LPCWSTR);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT); BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT*);
BOOL OpenClipboard(HWND); BOOL CloseClipboard(); BOOL EmptyClipboard(); HANDLE SetClipboardData(UINT, HANDLE); HANDLE GetClipboardData(UINT);
HGLOBAL GlobalAlloc(UINT, size_t); LPVOID GlobalLock(HGLOBAL); BOOL GlobalUnlock(HGLOBAL);
HINSTANCE ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int);
LONG RegGetValueW(HKEY, LPCWSTR, LPCWSTR, DWORD, DWORD*, void*, DWORD*);
HANDLE CreateThread(void*, size_t, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD*); DWORD WaitForSingleObject(HANDLE, DWORD); BOOL CloseHandle(HANDLE);
int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int); int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int, const char*, BOOL*);
HRESULT CoInitializeEx(void*, DWORD); void CoUninitialize(); void CoTaskMemFree(void*); LPWSTR GetCommandLineW(); LPWSTR* CommandLineToArgvW(LPCWSTR, int*);
BOOL GetOpenFileNameW(OPENFILENAMEW*); BOOL GetSaveFileNameW(OPENFILENAMEW*); PIDLIST_ABSOLUTE SHBrowseForFolderW(BROWSEINFOW*); BOOL SHGetPathFromIDListW(PIDLIST_ABSOLUTE, LPWSTR);
HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
UINT DragQueryFileW(HDROP, UINT, LPWSTR, UINT); void DragFinish(HDROP);
HRESULT DwmSetWindowAttribute(HWND, DWORD, const void*, DWORD); HRESULT SetWindowTheme(HWND, LPCWSTR, LPCWSTR);
int EnumFontFamiliesExW(HDC, LOGFONTW*, FONTENUMPROCW, LPARAM, DWORD);
BOOL InitCommonControlsEx(const INITCOMMONCONTROLSEX*); BOOL SetWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR, DWORD_PTR); BOOL RemoveWindowSubclass(HWND, SUBCLASSPROC, UINT_PTR); LRESULT DefSubclassProc(HWND, UINT, WPARAM, LPARAM);
errno_t wcsncpy_s(wchar_t*, size_t, const wchar_t*, size_t);
template <size_t N> errno_t wcsncpy_s(wchar_t (&)[N], const wchar_t*, size_t);
template <size_t N> errno_t wcscpy_s(wchar_t (&)[N], const wchar_t*); errno_t wcscpy_s(wchar_t*, size_t, const wchar_t*); int _wtoi(const wchar_t*);
LPVOID LocalFree(LPVOID);
}
