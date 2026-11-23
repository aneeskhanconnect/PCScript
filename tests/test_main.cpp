// PCScript test runner. Executes every case for real through lexer -> parser -> analyser -> interpreter.
//
// Case file format (tests/**/*.cases):
//   ### name
//   <program source>
//   >>> in            (optional: text fed to INPUT)
//   <input lines>
//   >>> out           (expected standard output, exact)
//   <expected lines>
// or, instead of ">>> out":
//   >>> error CODE [text that the message must contain]
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "common/diagnostics.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "runtime/interpreter.hpp"
#include "semantic/semantic.hpp"
#ifndef _WIN32
#include <pthread.h>
#endif

namespace fs = std::filesystem;
static int g_pass = 0, g_fail = 0;

static void report(bool ok, const std::string& name, const std::string& detail = "") {
    if (ok) ++g_pass; else { ++g_fail; std::cout << "FAIL: " << name << "\n" << detail << "\n"; }
}

struct Outcome { std::string out, errCode, errMsg; };

static Outcome execute(const std::string& src, const std::string& input, const std::string& file = "test.pcs",
                       pcs::Options opt = {}) {
    Outcome r;
    opt.hasSeed = true; opt.seed = 42; opt.autoFlush = false;
    try {
        pcs::Program prog = pcs::parseSource(src, file);
        auto diags = pcs::analyze(prog);
        for (const auto& d : diags) if (d.sev == pcs::Severity::Error) { r.errCode = d.code; r.errMsg = d.message; return r; }
        std::istringstream in(input); std::ostringstream out;
        try { pcs::Interpreter it(prog, in, out, opt); it.run(); }
        catch (const pcs::PcsError& e) { r.out = out.str(); r.errCode = e.d.code; r.errMsg = e.d.message; return r; }
        r.out = out.str();
    } catch (const pcs::PcsError& e) { r.errCode = e.d.code; r.errMsg = e.d.message; }
    return r;
}

static std::string readAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

static void runCaseFile(const fs::path& path) {
    std::istringstream in(readAll(path));
    std::string line, name, prog, input, expect, expectErr; int mode = -1;
    auto flush = [&]() {
        if (name.empty()) return;
        std::string full = path.filename().string() + " :: " + name;
        Outcome o = execute(prog, input);
        if (!expectErr.empty()) {
            std::istringstream es(expectErr); std::string code, rest; es >> code; std::getline(es, rest);
            if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            bool ok = o.errCode == code && (rest.empty() || o.errMsg.find(rest) != std::string::npos);
            report(ok, full, "  expected error " + expectErr + "\n  got code '" + o.errCode + "' message: " + o.errMsg);
        } else {
            bool ok = o.errCode.empty() && o.out == expect;
            report(ok, full, "  expected:\n" + expect + "  got:\n" + o.out + (o.errCode.empty() ? "" : "  error " + o.errCode + ": " + o.errMsg + "\n"));
        }
        name.clear(); prog.clear(); input.clear(); expect.clear(); expectErr.clear(); mode = -1;
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("### ", 0) == 0) { flush(); name = line.substr(4); mode = 0; continue; }
        if (line == ">>> in") { mode = 1; continue; }
        if (line == ">>> out") { mode = 2; continue; }
        if (line.rfind(">>> error ", 0) == 0) { expectErr = line.substr(10); mode = 3; continue; }
        if (mode == 0) prog += line + "\n"; else if (mode == 1) input += line + "\n"; else if (mode == 2) expect += line + "\n";
    }
    flush();
}

static void lexerTests() {
    pcs::Lexer lx("DECLARE Age : INTEGER\nAge <- 3.5 // note\nOUTPUT \"hi\", 'c', Age <> 2 \xE2\x86\x90", "t.pcs");
    auto t = lx.tokenize();
    report(t[0].t == pcs::Tok::Keyword && t[0].text == "DECLARE", "lexer: keyword");
    report(t[1].t == pcs::Tok::Ident && t[1].key == "age", "lexer: identifier key is lower-case");
    bool sawReal = false, sawStr = false, sawChar = false, sawNe = false, sawArrow = 0; int arrows = 0;
    for (auto& k : t) {
        sawReal |= k.t == pcs::Tok::Real && k.r == 3.5; sawStr |= k.t == pcs::Tok::Str && k.text == "hi";
        sawChar |= k.t == pcs::Tok::Char && k.text == "c"; sawNe |= k.t == pcs::Tok::Op && k.text == "<>";
        arrows += k.t == pcs::Tok::Op && k.text == "<-";
    }
    (void)sawArrow;
    report(sawReal && sawStr && sawChar && sawNe, "lexer: literals and operators");
    report(arrows == 2, "lexer: both <- and the arrow glyph give the assignment operator");
    bool comment = true; for (auto& k : t) if (k.text == "note") comment = false;
    report(comment, "lexer: comments are skipped");
    int lastLine = 0; for (auto& k : t) if (k.t != pcs::Tok::Newline && k.t != pcs::Tok::End) lastLine = k.line;
    report(lastLine == 3, "lexer: line numbers tracked");
    bool thrown = false; try { pcs::Lexer("OUTPUT \"abc", "t").tokenize(); } catch (const pcs::PcsError& e) { thrown = e.d.code == "E101"; }
    report(thrown, "lexer: unterminated string raises E101");
    thrown = false; try { pcs::Lexer("x <- 'ab'", "t").tokenize(); } catch (const pcs::PcsError& e) { thrown = e.d.code == "E101"; }
    report(thrown, "lexer: multi-character CHAR literal raises E101");
}

static void diagnosticTests() {
    try { pcs::parseSource("IF TRUE THEN\n  OUTPUT 1\nENDWHILE\n", "Main.pcs"); report(false, "diagnostics: mismatched end"); }
    catch (const pcs::PcsError& e) {
        std::string txt = pcs::formatDiagnostic(e.d, "IF TRUE THEN\n  OUTPUT 1\nENDWHILE\n");
        report(e.d.code == "E102" && e.d.line == 3 && e.d.col == 1, "diagnostics: line/column of mismatched end", txt);
        report(txt.find("File: Main.pcs") != std::string::npos && txt.find("Line: 3") != std::string::npos &&
               txt.find("Unexpected token: ENDWHILE") != std::string::npos && txt.find("Expected: ENDIF") != std::string::npos &&
               txt.find("ENDWHILE\n") != std::string::npos && txt.find('^') != std::string::npos,
               "diagnostics: formatted block has file, line, excerpt, caret, expected token", txt);
        std::string m = pcs::formatMachine(e.d);
        report(m.rfind("Main.pcs:3:1: error E102:", 0) == 0, "diagnostics: machine format for the IDE", m);
    }
}

static void optionTests() {
    pcs::Options o; o.timeoutMs = 100;
    Outcome r = execute("DECLARE x : INTEGER\nx <- 0\nWHILE TRUE DO\n x <- x + 1\nENDWHILE\n", "", "t.pcs", o);
    report(r.errCode == "R310", "runtime: execution timeout stops an infinite loop (R310)", r.errCode);
    std::ostringstream tr; pcs::Options t; t.trace = true; t.traceOut = &tr;
    execute("OUTPUT 1\nOUTPUT 2\n", "", "t.pcs", t);
    report(tr.str() == "[trace] line 1\n[trace] line 2\n", "runtime: tracing reports each executed line", tr.str());
    pcs::Options d; d.maxDepth = 50;
    r = execute("PROCEDURE R(n : INTEGER)\n CALL R(n + 1)\nENDPROCEDURE\nCALL R(1)\n", "", "t.pcs", d);
    report(r.errCode == "R307", "runtime: runaway recursion gives R307 instead of crashing", r.errCode);
#ifndef __SANITIZE_ADDRESS__   // ASan's own no-return handler faults when unwinding a 5000-deep ASan-inflated stack
    r = execute("PROCEDURE R(n : INTEGER)\n CALL R(n + 1)\nENDPROCEDURE\nCALL R(1)\n", "");
    report(r.errCode == "R307", "runtime: default depth limit stops runaway recursion safely", r.errCode);
#endif
}

static void exampleTests() {
    fs::path ex = PCS_EXAMPLES_DIR, exp = fs::path(PCS_TEST_DIR) / "runtime" / "examples";
    int n = 0;
    for (auto& e : fs::directory_iterator(ex)) {
        if (e.path().extension() != ".pcs") continue;
        ++n;
        pcs::Options opt; opt.baseDir = ex.string();
        Outcome o = execute(readAll(e.path()), "", e.path().filename().string(), opt);
        std::string want = readAll(exp / (e.path().stem().string() + ".expected"));
        for (size_t p; (p = want.find('\r')) != std::string::npos;) want.erase(p, 1);
        report(o.errCode.empty() && o.out == want, "example " + e.path().filename().string(), "  expected:\n" + want + "  got:\n" + o.out + o.errCode + " " + o.errMsg);
    }
    report(n >= 8, "examples: at least eight example programs are present");
}

static int realMain() {
    lexerTests(); diagnosticTests(); optionTests(); exampleTests();
    for (const char* dir : {"parser", "language", "runtime"})
        for (auto& e : fs::directory_iterator(fs::path(PCS_TEST_DIR) / dir))
            if (e.path().extension() == ".cases") runCaseFile(e.path());
    std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}

#if defined(_WIN32) || defined(__SANITIZE_ADDRESS__)   // ASan mis-handles huge pthread stacks; use ulimit -s there
int main() { return realMain(); }
#else
static void* threadMain(void* p) { *static_cast<int*>(p) = realMain(); return nullptr; }
int main() {
    int rc = 1; pthread_attr_t attr; pthread_attr_init(&attr); pthread_t th;
    if (pthread_attr_setstacksize(&attr, static_cast<size_t>(256) << 20) == 0 && pthread_create(&th, &attr, threadMain, &rc) == 0) { pthread_join(th, nullptr); return rc; }
    return realMain();
}
#endif
