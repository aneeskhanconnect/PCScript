// pcscript — PCScript compiler/interpreter command line.
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "common/diagnostics.hpp"
#include "parser/parser.hpp"
#include "runtime/interpreter.hpp"
#include "semantic/semantic.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

static const char* kVersion = "2.0.0";

static void usage() {
    std::cout << "PCScript " << kVersion << " - Cambridge pseudocode interpreter\n\n"
                 "Usage: pcscript [options] <file.pcs>\n\n"
                 "Options:\n"
                 "  --check            Parse and analyse only; do not run\n"
                 "  --machine          Print diagnostics as file:line:col: error CODE: message\n"
                 "  --trace            Write \"[trace] line N\" to stderr before each statement\n"
                 "  --timeout=MS       Stop after MS milliseconds of execution\n"
                 "  --seed=N           Seed the random number generator (RAND)\n"
                 "  --max-depth=N      Maximum call depth (default 5000)\n"
                 "  --version, --help\n\n"
                 "Exit codes: 0 success, 1 compile error, 2 runtime error, 3 file/usage error\n";
}

static int realMain(int argc, char** argv) {
    bool check = false, machine = false, trace = false;
    pcs::Options opt; std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--version") { std::cout << "pcscript " << kVersion << "\n"; return 0; }
        else if (a == "--check") check = true;
        else if (a == "--machine") machine = true;
        else if (a == "--trace") trace = true;
        else if (a.rfind("--timeout=", 0) == 0) opt.timeoutMs = std::atoll(a.c_str() + 10);
        else if (a.rfind("--seed=", 0) == 0) { opt.hasSeed = true; opt.seed = std::strtoull(a.c_str() + 7, nullptr, 10); }
        else if (a.rfind("--max-depth=", 0) == 0) opt.maxDepth = std::atoi(a.c_str() + 12);
        else if (!a.empty() && a[0] == '-') { std::cerr << "Unknown option: " << a << "\n"; return 3; }
        else path = a;
    }
    if (path.empty()) {
        usage();
        std::cout << "\nTip: pcscript is a command-line tool. Open Command Prompt in this folder and run:\n"
                     "    pcscript examples\\01_variables.pcs\n"
                     "or drag a .pcs file onto pcscript.exe.\n";
        return 3;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "pcscript: cannot open file '" << path << "'\n"; return 3; }
    std::stringstream ss; ss << f.rdbuf();
    std::string src = ss.str();

    std::string name = path;
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) { name = path.substr(slash + 1); opt.baseDir = path.substr(0, slash); }
    opt.trace = trace; opt.traceOut = &std::cerr;

    auto show = [&](const pcs::Diagnostic& d) {
        if (machine) std::cerr << pcs::formatMachine(d) << "\n";
        else std::cerr << pcs::formatDiagnostic(d, src) << "\n";
    };
    try {
        pcs::Program prog = pcs::parseSource(src, name);
        auto diags = pcs::analyze(prog);
        for (const auto& d : diags) show(d);
        if (pcs::hasErrors(diags)) return 1;
        if (check) { if (!machine) std::cerr << "No errors found in " << name << ".\n"; return 0; }
        pcs::Interpreter interp(prog, std::cin, std::cout, opt);
        interp.run();
        return 0;
    } catch (const pcs::PcsError& e) {
        std::cout.flush();
        show(e.d);
        return e.d.category == "Runtime error" ? 2 : 1;
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << "pcscript: internal error: " << e.what() << "\n";
        return 2;
    }
}

// Deep PCScript recursion needs a large C++ stack. On Windows the linker option /STACK (see CMakeLists.txt)
// enlarges the main thread; elsewhere the interpreter runs on a dedicated 256 MB thread.
#ifdef _WIN32
// When the exe is started by double-click or drag-and-drop, Windows creates a console just for it and closes
// that console the moment the program exits. Detect this (we are the only process on the console) and wait.
static bool ownsConsole() {
    DWORD pids[2];
    return GetConsoleProcessList(pids, 2) == 1 && _isatty(0);   // only wait when a person is at the keyboard
}
int main(int argc, char** argv) {
    int rc = realMain(argc, argv);
    if (ownsConsole()) {
        std::cout << "\n[Program finished with exit code " << rc << "]\nPress Enter to close this window..." << std::flush;
        std::string line; std::getline(std::cin, line);
    }
    return rc;
}
#else
namespace { struct Args { int argc; char** argv; int rc; }; }
static void* threadMain(void* p) { auto* a = static_cast<Args*>(p); a->rc = realMain(a->argc, a->argv); return nullptr; }
int main(int argc, char** argv) {
    Args a{argc, argv, 0};
    pthread_attr_t attr; pthread_attr_init(&attr);
    pthread_t th;
    if (pthread_attr_setstacksize(&attr, static_cast<size_t>(256) << 20) == 0 && pthread_create(&th, &attr, threadMain, &a) == 0) {
        pthread_join(th, nullptr);
        return a.rc;
    }
    return realMain(argc, argv);
}
#endif
