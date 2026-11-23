// PCScript Studio entry point.
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include "app.hpp"
#include "win_util.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES |
                                              ICC_TREEVIEW_CLASSES | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    AppOptions opt;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::string a = win::narrow(argv[i]);
        auto next = [&]() { return i + 1 < argc ? win::narrow(argv[++i]) : std::string(); };
        if (a == "--selftest") opt.selfTest = true;
        else if (a == "--data-dir") opt.dataDir = next();
        else if (a == "--docs-dir") opt.docsDir = next();
        else if (a == "--selftest-log") opt.selfTestLog = next();
        else if (!a.empty() && a[0] != '-') opt.files.push_back(a);
    }
    if (argv) LocalFree(argv);

    int rc;
    {
        App app(opt);
        rc = app.run(nCmdShow);
    }
    CoUninitialize();
    return rc;
}
