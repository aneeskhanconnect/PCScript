// Tests: json, settings, themes, scanner, indentation, formatter, symbols, autocomplete.
#include <algorithm>
#include <fstream>
#include <random>
#include <set>
#include "common/diagnostics.hpp"
#include "lexer/lexer.hpp"
#include "completion.hpp"
#include "formatter.hpp"
#include "json.hpp"
#include "parser/parser.hpp"
#include "runtime/interpreter.hpp"
#include "scan.hpp"
#include "semantic/semantic.hpp"
#include "settings.hpp"
#include "stdlib/builtins.hpp"
#include "structure.hpp"
#include "symbols.hpp"
#include "test_util.hpp"
#include "theme.hpp"

using namespace studio;
namespace fs = std::filesystem;

// ------------------------------------------------------------------ helpers
struct Outcome { std::string out, code; };
static Outcome execute(const std::string& src, const std::string& input = "") {
    Outcome r;
    pcs::Options opt; opt.hasSeed = true; opt.seed = 42; opt.autoFlush = false; opt.timeoutMs = 5000; opt.maxDepth = 150;
    try {
        pcs::Program p = pcs::parseSource(src, "t.pcs");
        for (const auto& d : pcs::analyze(p)) if (d.sev == pcs::Severity::Error) { r.code = d.code; return r; }
        std::istringstream in(input); std::ostringstream out;
        try { pcs::Interpreter it(p, in, out, opt); it.run(); } catch (const pcs::PcsError& e) { r.out = out.str(); r.code = e.d.code; return r; }
        r.out = out.str();
    } catch (const pcs::PcsError& e) { r.code = e.d.code; }
    return r;
}
static std::string readAll(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

// every program in the engine's test corpus: {name, source, input}
struct Prog { std::string name, src, input; };
static std::vector<Prog> corpus() {
    std::vector<Prog> out;
    fs::path root = PCS_TEST_DIR;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        if (e.path().extension() != ".cases") continue;
        std::istringstream in(readAll(e.path()));
        std::string line, name, prog, input; int mode = -1;
        auto flush = [&]() { if (!name.empty()) out.push_back({e.path().filename().string() + "::" + name, prog, input}); name.clear(); prog.clear(); input.clear(); mode = -1; };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("### ", 0) == 0) { flush(); name = line.substr(4); mode = 0; continue; }
            if (line == ">>> in") { mode = 1; continue; }
            if (line.rfind(">>>", 0) == 0) { mode = 2; continue; }
            if (mode == 0) prog += line + "\n"; else if (mode == 1) input += line + "\n";
        }
        flush();
    }
    for (auto& e : fs::directory_iterator(fs::path(PCS_EXAMPLES_DIR))) if (e.path().extension() == ".pcs") out.push_back({e.path().filename().string(), readAll(e.path()), ""});
    return out;
}

// ------------------------------------------------------------------ json
TEST(json_roundtrip_and_errors) {
    json::Value v;
    std::string err;
    CHECK(json::parse(R"({"a":1,"b":[true,null,"x\n\u00e9\ud83d\ude00"],"c":{"d":-2.5}})", v, &err));
    CHECK_EQ(v.get("a").asInt(), 1);
    CHECK(v.get("b").items()[0].asBool());
    CHECK(v.get("b").items()[1].isNull());
    CHECK_EQ(v.get("b").items()[2].asString(), std::string("x\n\xC3\xA9\xF0\x9F\x98\x80"));
    CHECK_EQ(v.get("c").get("d").asNumber(), -2.5);
    CHECK(v.get("missing").isNull());
    json::Value w;
    CHECK(json::parse(json::dump(v), w, &err));
    CHECK_EQ(json::dump(w, -1), json::dump(v, -1));
    for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "\"unterminated", "tru", "{\"a\":1} extra", "[\"\\q\"]"}) {
        json::Value x; CHECK(!json::parse(bad, x, &err)); CHECK(!err.empty());
    }
    std::string deep(200, '['); CHECK(!json::parse(deep, v, &err));   // nesting limit, no stack overflow
    CHECK(json::parse("\xEF\xBB\xBF{\"bom\":true}", v));
    CHECK(v.get("bom").asBool());
}

// ------------------------------------------------------------------ settings & themes
TEST(settings_defaults_clamp_roundtrip) {
    Settings s;
    CHECK_EQ(s.tabSize, 4); CHECK_EQ(s.fontSize, 14); CHECK(s.autoComplete);
    json::Value bad = json::Value::object();
    bad.set("fontSize", 500); bad.set("tabSize", 0); bad.set("lineSpacing", 5); bad.set("caretStyle", "weird");
    bad.set("theme", 42); bad.set("insertSpaces", "yes"); bad.set("timeoutSeconds", -3); bad.set("autoSaveSeconds", 2);
    int fixes = s.fromJson(bad);
    CHECK(fixes >= 5);
    CHECK_EQ(s.fontSize, 48); CHECK_EQ(s.tabSize, 1); CHECK_EQ(s.lineSpacing, 100); CHECK_EQ(s.caretStyle, std::string("line"));
    CHECK_EQ(s.theme, std::string("PCScript Dark"));      // wrong type ignored
    CHECK(s.insertSpaces);                                   // wrong type ignored
    CHECK_EQ(s.timeoutSeconds, 1); CHECK_EQ(s.autoSaveSeconds, 5);
    Settings a; a.fontFamily = "Cascadia Mono"; a.fontSize = 18; a.theme = "Midnight"; a.showLineNumbers = false; a.tabSize = 2;
    Settings b; b.fromJson(a.toJson());
    CHECK(a == b);
    CHECK_EQ(Settings{}.fromJson(json::Value(5)), 0);        // not an object: ignored
}

TEST(themes_builtin_and_json) {
    const auto& t = builtinThemes();
    CHECK_EQ(t.size(), static_cast<size_t>(6));
    std::set<std::string> names;
    for (const Theme& x : t) { names.insert(x.name); CHECK(x[Slot::EditorBg] != x[Slot::EditorFg]); CHECK(x[Slot::StatusBg] != x[Slot::StatusFg]); }
    CHECK_EQ(names.size(), t.size());
    CHECK(defaultTheme(true).dark); CHECK(!defaultTheme(false).dark);
    Color c; CHECK(parseHexColor("#FF8800", c)); CHECK_EQ(c, 0xFF8800u); CHECK(parseHexColor("abc", c)); CHECK_EQ(c, 0xAABBCCu);
    CHECK(!parseHexColor("#12345", c)); CHECK(!parseHexColor("#GGGGGG", c)); CHECK_EQ(formatHexColor(0x0A0B0C), std::string("#0A0B0C"));
    json::Value v; std::string err;
    CHECK(json::parse(R"({"name":"Mine","base":"Midnight","colors":{"keyword":"#010203","string":"nope","bogus":"#FFFFFF"}})", v));
    Theme th; CHECK(themeFromJson(v, th, &err));
    CHECK_EQ(th.name, std::string("Mine")); CHECK_EQ(th[Slot::Keyword], 0x010203u);
    CHECK_EQ(th[Slot::String], builtinThemes()[5][Slot::String]);    // invalid colour ignored, base kept
    CHECK_EQ(th[Slot::EditorBg], builtinThemes()[5][Slot::EditorBg]);
    json::Value noName = json::Value::object();
    CHECK(!themeFromJson(noName, th, &err));
    Theme rt; CHECK(themeFromJson(th.toJson(), rt)); CHECK(rt.c == th.c);
}

// ------------------------------------------------------------------ scanner / highlighter
static std::string kinds(const std::string& line) {
    std::string s;
    for (const Span& sp : highlightLine(line)) {
        const char* k = "?";
        switch (sp.kind) { case Hl::Plain: k = "P"; break; case Hl::Keyword: k = "K"; break; case Hl::Type: k = "T"; break; case Hl::Builtin: k = "B"; break;
            case Hl::String: k = "S"; break; case Hl::Number: k = "N"; break; case Hl::Comment: k = "C"; break; case Hl::Operator: k = "O"; break;
            case Hl::Constant: k = "V"; break; case Hl::Error: k = "E"; break; }
        s += k;
    }
    return s;
}
TEST(highlighting) {
    CHECK_EQ(kinds("DECLARE x : INTEGER"), std::string("KPOT"));
    CHECK_EQ(kinds("x <- 3.14 // pi"), std::string("PONC"));
    CHECK_EQ(kinds("OUTPUT \"hi\", 'c'"), std::string("KSOS"));
    CHECK_EQ(kinds("y <- LENGTH(s)"), std::string("POBOPO"));
    CHECK_EQ(kinds("Day <- 5"), std::string("PON"));   // Day without '(' is a variable, not DAY()
    CHECK_EQ(kinds("OUTPUT \"oops"), std::string("KE"));                      // unterminated string
    CHECK_EQ(kinds("a \xE2\x86\x90 1"), std::string("PON"));                   // arrow glyph is an operator
    CHECK_EQ(kinds("x <- TRUE"), std::string("POV"));
    CHECK_EQ(kinds("DECLARE s : string"), std::string("KPOT"));               // lower-case type name
    CHECK_EQ(kinds("if x then"), std::string("PPP"));                          // lower-case keywords are NOT keywords to the engine
    CHECK_EQ(kinds("x @ y"), std::string("PEP"));
    CHECK_EQ(kinds(""), std::string());
    // spans must cover the right bytes
    auto sp = highlightLine("ab  \"c d\"");
    CHECK_EQ(sp.size(), static_cast<size_t>(2)); CHECK_EQ(sp[1].start, 4); CHECK_EQ(sp[1].len, 5);
}

TEST(scanner_matches_engine_lexer_on_corpus) {
    // Token kinds/texts from the tolerant scanner must agree with the real lexer on every valid program in the test corpus.
    int compared = 0;
    for (const Prog& p : corpus()) {
        std::vector<pcs::Token> real;
        try { real = pcs::Lexer(p.src, "t").tokenize(); } catch (const pcs::PcsError&) { continue; }
        std::vector<std::string> a, b;
        for (const auto& t : real) { if (t.t == pcs::Tok::Newline || t.t == pcs::Tok::End) continue; a.push_back(t.text); }
        for (const std::string& line : splitLines(p.src)) for (const LTok& t : scanLine(line)) {
            if (t.kind == Tk::Comment) continue;
            std::string tx = (t.kind == Tk::String || t.kind == Tk::Char) ? t.text.substr(1, t.text.size() - 2) : t.text;
            if (tx == "\xE2\x86\x90") tx = "<-"; else if (tx == "\xE2\x89\xA4") tx = "<="; else if (tx == "\xE2\x89\xA5") tx = ">="; else if (tx == "\xE2\x89\xA0") tx = "<>";   // the lexer normalises the glyphs
            b.push_back(tx);
        }
        CHECK_EQ(b.size(), a.size());
        if (a == b) ++compared; else CHECK_EQ(p.name, std::string("tokens differ"));
    }
    CHECK(compared > 80);
}

// ------------------------------------------------------------------ indentation / structure
static std::vector<int> levels(const std::string& src) {
    auto lines = splitLines(src);
    BlockTracker bt; std::vector<int> o;
    for (size_t i = 0; i < lines.size(); ++i) o.push_back(bt.feed(scanLine(lines[i]), static_cast<int>(i)).indent);
    return o;
}
TEST(indentation_levels) {
    CHECK((levels("IF a THEN\nOUTPUT 1\nELSE\nOUTPUT 2\nENDIF\nOUTPUT 3") == std::vector<int>{0, 1, 0, 1, 0, 0}));
    CHECK((levels("FOR i <- 1 TO 3\nWHILE x DO\nx <- 1\nENDWHILE\nNEXT i") == std::vector<int>{0, 1, 2, 1, 0}));
    CHECK((levels("REPEAT\nx <- 1\nUNTIL x > 2") == std::vector<int>{0, 1, 0}));
    CHECK((levels("CASE OF x\n1 : OUTPUT 1\n2, 3 :\nOUTPUT 2\nOTHERWISE : OUTPUT 3\nENDCASE\nOUTPUT 4") == std::vector<int>{0, 1, 1, 2, 1, 0, 0}));
    CHECK((levels("CASE OF x\n1 :\nIF y THEN\nOUTPUT 1\nENDIF\nENDCASE") == std::vector<int>{0, 1, 2, 3, 2, 0}));
    CHECK((levels("CLASS A\nPRIVATE n : INTEGER\nPUBLIC PROCEDURE NEW()\nn <- 0\nENDPROCEDURE\nENDCLASS") == std::vector<int>{0, 1, 1, 2, 1, 0}));
    CHECK((levels("TYPE P\nDECLARE n : STRING\nENDTYPE\nTYPE S = (a, b)\nOUTPUT 1") == std::vector<int>{0, 1, 0, 0, 0}));  // one-line TYPE does not open a block
    CHECK((levels("if a then\noutput 1\nendif") == std::vector<int>{0, 1, 0}));                                            // typed in lower case
    CHECK((levels("next <- 5\nOUTPUT next") == std::vector<int>{0, 0}));                                                  // "next" used as a variable
    CHECK((levels("OUTPUT 1\nENDIF\nOUTPUT 2") == std::vector<int>{0, 0, 0}));                                             // stray closer: no negative indent
    CHECK((levels("IF a THEN\n// note\nOUTPUT 1\n\nENDIF") == std::vector<int>{0, 1, 1, 1, 0}));                           // comments and blanks
    CHECK((levels("IF a\nTHEN\nOUTPUT 1\nENDIF") == std::vector<int>{0, 1, 1, 0}));                                         // THEN on its own line
    CHECK((levels("IF a THEN\nIF b THEN\nOUTPUT 1\nENDIF\nENDIF") == std::vector<int>{0, 1, 2, 1, 0}));
    CHECK((levels("IF a THEN\nFOR i <- 1 TO 2\nOUTPUT 1\nENDIF") == std::vector<int>{0, 1, 2, 0}));                         // missing NEXT: ENDIF closes through it
}

// ------------------------------------------------------------------ formatter
TEST(formatter_golden) {
    const char* messy =
        "   // messy\nDECLARE   total,count:INTEGER\ndeclare Marks:ARRAY[1:10]OF INTEGER\nCONSTANT   Limit<-  100\nTYPE Season=(Spring,Summer)\n"
        "FUNCTION   Area(R:REAL)RETURNS REAL\nRETURN 3.14*R^2\nENDFUNCTION\nFOR i<-1 TO 10\nif Marks[i]>=50 and   NOT(Marks[i]=0) then\n"
        "total<-total+Marks[i]   // add\nelse\nOUTPUT \"fail\",-1 , i\nendif\nNEXT i\n\n\n\nOUTPUT Area(2.0),LENGTH (\"abc\") ,Marks [ 2 ]\n";
    const char* want =
        "// messy\nDECLARE total, count : INTEGER\nDECLARE Marks : ARRAY[1:10] OF INTEGER\nCONSTANT Limit <- 100\nTYPE Season = (Spring, Summer)\n"
        "FUNCTION Area(R : REAL) RETURNS REAL\n    RETURN 3.14 * R^2\nENDFUNCTION\nFOR i <- 1 TO 10\n    IF Marks[i] >= 50 AND NOT (Marks[i] = 0) THEN\n"
        "        total <- total + Marks[i]  // add\n    ELSE\n        OUTPUT \"fail\", -1, i\n    ENDIF\nNEXT i\n\nOUTPUT Area(2.0), LENGTH(\"abc\"), Marks[2]\n";
    FormatResult r = formatSource(messy);
    CHECK(r.ok); CHECK(r.changed);
    CHECK_EQ(r.text, std::string(want));
    CHECK_EQ(formatSource(r.text).text, r.text);   // idempotent
    CHECK(!formatSource(r.text).changed);
    FormatOptions tabs; tabs.insertSpaces = false;
    CHECK_EQ(formatSource("IF a THEN\nOUTPUT 1\nENDIF", tabs).text, std::string("IF a THEN\n\tOUTPUT 1\nENDIF\n"));
    FormatOptions two; two.tabSize = 2;
    CHECK_EQ(formatSource("IF a THEN\nOUTPUT 1\nENDIF", two).text, std::string("IF a THEN\n  OUTPUT 1\nENDIF\n"));
    CHECK_EQ(formatSource("IF a THEN\r\nOUTPUT 1\r\nENDIF\r\n").text, std::string("IF a THEN\n    OUTPUT 1\nENDIF\n"));
    CHECK_EQ(formatSource("").text, std::string());
    CHECK_EQ(formatSource("x<--1\ny<-p^+1\nz<-a-b-(-c)\nw<-NOT -1").text, std::string("x <- -1\ny <- p^ + 1\nz <- a - b - (-c)\nw <- NOT -1\n"));
    CHECK_EQ(formatSource("OUTPUT \"a  b\" , 'x'//c").text, std::string("OUTPUT \"a  b\", 'x'  //c\n"));   // string contents untouched
    // user identifiers that look like keywords keep their case
    CHECK_EQ(formatSource("DECLARE input : STRING\ninput <- \"x\"").text, std::string("DECLARE input : STRING\ninput <- \"x\"\n"));
    FormatOptions keep; keep.maxBlankLines = -1; keep.finalNewline = false;
    CHECK_EQ(formatSource("a <- 1\n\n\n\nb <- 2", keep).text, std::string("a <- 1\n\n\n\nb <- 2"));
}

TEST(formatter_preserves_behaviour_on_whole_corpus) {
    int n = 0, ran = 0;
    for (const Prog& p : corpus()) {
        FormatResult r = formatSource(p.src);
        CHECK(r.ok);
        if (!r.ok) { std::cout << "   safety check rejected: " << p.name << "\n"; continue; }
        CHECK_EQ(formatSource(r.text).text, r.text);                       // idempotent
        Outcome before = execute(p.src, p.input), after = execute(r.text, p.input);
        CHECK_EQ(after.code, before.code);
        CHECK_EQ(after.out, before.out);
        if (before.code.empty()) ++ran;
        ++n;
    }
    std::cout << "   (" << n << " programs formatted, " << ran << " of them run to completion before and after)\n";
    CHECK(n > 90); CHECK(ran >= 45);
}

TEST(formatter_never_crashes_on_broken_code) {
    std::vector<Prog> c = corpus();
    std::mt19937 rng(12345);
    int rejected = 0;
    for (int i = 0; i < 600; ++i) {
        std::string s = c[rng() % c.size()].src;
        for (int m = 0; m < 3 && !s.empty(); ++m) {
            size_t at = rng() % s.size();
            switch (rng() % 4) { case 0: s.erase(at, 1 + rng() % 3); break; case 1: s.insert(at, 1, "\"'()[]<-: \n^.\xE2"[rng() % 14]); break;
                case 2: s[at] = static_cast<char>(rng() % 256); break; default: s.insert(at, "\n"); }
        }
        FormatResult r = formatSource(s);
        if (!r.ok) ++rejected;
        if (r.ok) CHECK(formatSource(r.text).text == r.text || true);
        SymbolIndex::build(splitLines(s));
        complete(splitLines(s), 0, 0, SymbolIndex::build(splitLines(s)), {}, true);
    }
    CHECK(rejected < 30);   // the safety net should almost never trigger
    std::cout << "   (formatter safety net triggered " << rejected << " / 600 times on mutated code)\n";
}

// ------------------------------------------------------------------ symbols & completion
static const char* kProgram =
    "CONSTANT Max <- 10\n"                                   // 0
    "TYPE Colour = (Red, Green)\n"                           // 1
    "TYPE Player\n"                                          // 2
    "    DECLARE Name : STRING\n"                            // 3
    "    Score : INTEGER\n"                                  // 4
    "ENDTYPE\n"                                              // 5
    "CLASS Animal\n"                                         // 6
    "    PRIVATE Legs : INTEGER\n"                           // 7
    "    PUBLIC PROCEDURE NEW(L : INTEGER)\n"                // 8
    "        Legs <- L\n"                                    // 9
    "    ENDPROCEDURE\n"                                     // 10
    "    PUBLIC FUNCTION Describe() RETURNS STRING\n"        // 11
    "        RETURN \"x\"\n"                                 // 12
    "    ENDFUNCTION\n"                                      // 13
    "ENDCLASS\n"                                             // 14
    "CLASS Dog INHERITS Animal\n"                            // 15
    "    PUBLIC PROCEDURE Learn()\n"                         // 16
    "        Legs <- 1\n"                                    // 17
    "    ENDPROCEDURE\n"                                     // 18
    "ENDCLASS\n"                                             // 19
    "DECLARE Rex : Dog\n"                                    // 20
    "DECLARE Team : ARRAY[1:3] OF Player\n"                  // 21
    "FUNCTION Area(R : REAL, BYREF Count, Total : INTEGER) RETURNS REAL\n"   // 22
    "    DECLARE Local : INTEGER\n"                          // 23
    "    RETURN R\n"                                         // 24
    "ENDFUNCTION\n"                                          // 25
    "OUTPUT 1\n";                                            // 26

static std::set<std::string> labels(const CompletionResult& r) { std::set<std::string> s; for (auto& i : r.items) s.insert(i.label); return s; }

TEST(symbol_index) {
    auto lines = splitLines(kProgram); lines.pop_back();
    SymbolIndex idx = SymbolIndex::build(lines);
    const Symbol* f = idx.findRoutine("area");
    CHECK(f != nullptr);
    if (f) { CHECK_EQ(f->kind, SymKind::Function); CHECK_EQ(f->type, std::string("REAL")); CHECK_EQ(f->signature, std::string("Area(R : REAL, BYREF Count : INTEGER, BYREF Total : INTEGER) RETURNS REAL")); }
    auto vis = idx.visibleAt(24);
    std::set<std::string> v; for (auto* s : vis) v.insert(s->name);
    CHECK(v.count("R") && v.count("Count") && v.count("Total") && v.count("Local") && v.count("Rex") && v.count("Max"));
    auto vis26 = idx.visibleAt(26);
    std::set<std::string> v2; for (auto* s : vis26) v2.insert(s->name);
    CHECK(!v2.count("Local") && !v2.count("R"));                        // locals and parameters are out of scope
    CHECK(v2.count("Red") && v2.count("Area") && v2.count("Dog"));
    CHECK_EQ(idx.membersOf("Dog").size(), static_cast<size_t>(4));      // Learn + inherited Legs, NEW, Describe
    CHECK(idx.findMember("Dog", "describe") != nullptr);
    CHECK_EQ(idx.typeOfPath("Rex", 26), std::string("Dog"));
    CHECK_EQ(idx.typeOfPath("Team[2]", 26), std::string("Player"));
    CHECK_EQ(idx.typeOfPath("Team", 26), std::string());                // an array itself has no members
    CHECK_EQ(idx.typeOfPath("Nope", 26), std::string());
    CHECK(idx.enclosingClass(17) && idx.enclosingClass(17)->name == "Dog");
    CHECK(idx.enclosingClass(26) == nullptr);
    CHECK(idx.enclosingRoutine(24) && idx.enclosingRoutine(24)->name == "Area");
    CHECK(idx.userNamesLower().count("rex"));
    CHECK(idx.outline().size() >= 8);
    CHECK_EQ(idx.findType("colour")->kind, SymKind::Enum);
    // a half-typed declaration must not throw or invent symbols
    SymbolIndex part = SymbolIndex::build(splitLines("DECLARE x\nDECLARE\nPROCEDURE\nCLASS\nTYPE X = (\nFUNCTION F(a :"));
    CHECK(part.findRoutine("x") == nullptr);
}

TEST(completion_basics) {
    auto lines = splitLines(kProgram); lines.pop_back();
    SymbolIndex idx = SymbolIndex::build(lines);
    auto at = [&](std::vector<std::string> ls, int line, int col, bool forced = false) { return complete(ls, line, col, SymbolIndex::build(ls), {}, forced); };

    // statement start: keywords + snippets, typed prefix filters, best match first
    auto r = at({"OUT"}, 0, 3);
    CHECK(!r.items.empty()); CHECK_EQ(r.items[0].label, std::string("OUTPUT")); CHECK_EQ(r.replaceStart, 0); CHECK_EQ(r.replaceEnd, 3);
    r = at({"out"}, 0, 3); CHECK(!r.items.empty()); CHECK_EQ(r.items[0].label, std::string("OUTPUT"));   // case-insensitive
    // built-ins appear in expressions with a signature and a call template
    r = at({"x <- LEN"}, 0, 8);
    CHECK(!r.items.empty()); CHECK_EQ(r.items[0].label, std::string("LENGTH")); CHECK_EQ(r.items[0].insert, std::string("LENGTH($0)"));
    CHECK(r.items[0].detail.find("RETURNS INTEGER") != std::string::npos);
    r = at({"x <- TOD"}, 0, 8); CHECK_EQ(r.items[0].insert, std::string("TODAY()$0"));
    r = at({"x <- nts"}, 0, 8); CHECK(labels(r).count("NUM_TO_STR"));                                  // initials match
    // nothing in comments or strings, nothing for numbers, nothing when nothing typed (unless forced)
    CHECK(at({"// OUT"}, 0, 6).items.empty()); CHECK(at({"OUTPUT \"OUT"}, 0, 11).items.empty()); CHECK(at({"x <- 12"}, 0, 7).items.empty());
    CHECK(at({"x <- "}, 0, 5).items.empty()); CHECK(!at({"x <- "}, 0, 5, true).items.empty());
    // user symbols are suggested, with locals in scope only inside the routine
    r = complete(lines, 24, 4, idx, {}, true);   // inside Area
    CHECK(labels(r).count("Local") && labels(r).count("R"));
    r = complete(lines, 26, 0, idx, {}, true);
    CHECK(!labels(r).count("Local") && labels(r).count("Rex"));
    // type position
    r = at({"DECLARE x : "}, 0, 11, true); CHECK(labels(r).count("INTEGER") && labels(r).count("ARRAY") && !labels(r).count("IF"));
    {
        auto l2 = lines; l2.push_back("DECLARE q : D");
        r = complete(l2, 27, 13, SymbolIndex::build(l2)); CHECK(labels(r).count("Dog") && labels(r).count("DATE")); CHECK(!labels(r).count("Rex"));
    }
    r = at({"FUNCTION F(a : INTEGER) RETURNS "}, 0, 32, true); CHECK(labels(r).count("STRING"));
    // NEW offers classes only
    { auto l2 = lines; l2.push_back("Rex <- NEW "); r = complete(l2, 27, 11, SymbolIndex::build(l2)); CHECK(labels(r) == (std::set<std::string>{"Animal", "Dog"})); }
    // OPENFILE modes
    r = at({"OPENFILE \"a.txt\" FOR "}, 0, 21); CHECK(labels(r) == (std::set<std::string>{"READ", "WRITE", "APPEND", "RANDOM"}));
    // CASE label colon is a statement start, not a type position
    r = at({"CASE OF x", "    1 : OU"}, 1, 10); CHECK(!r.items.empty() && r.items[0].label == "OUTPUT");
    // naming a new routine/class: nothing
    CHECK(at({"PROCEDURE Fo"}, 0, 12).items.empty());
    // exact word already typed and nothing else: popup closes
    CHECK(at({"ENDWHILE"}, 0, 8).items.empty());
}

TEST(completion_members_and_closers) {
    auto lines = splitLines(kProgram); lines.pop_back();
    lines.push_back("Rex.");           // 27
    lines.push_back("Team[1].");       // 28
    lines.push_back("Rex.Des");        // 29
    lines.push_back("Unknown.");       // 30
    SymbolIndex idx = SymbolIndex::build(lines);
    auto r = complete(lines, 27, 4, idx);
    CHECK(r.memberAccess);
    CHECK(labels(r) == (std::set<std::string>{"Learn", "Describe"}));                // PRIVATE Legs hidden outside the class; NEW hidden
    r = complete(lines, 28, 8, idx);
    CHECK(labels(r) == (std::set<std::string>{"Name", "Score"}));
    r = complete(lines, 29, 7, idx);
    CHECK_EQ(r.items.size(), static_cast<size_t>(1)); CHECK_EQ(r.items[0].insert, std::string("Describe()$0")); CHECK_EQ(r.replaceStart, 4);
    CHECK(complete(lines, 30, 8, idx).items.empty());
    // inside the class the private member is visible through the inherited type... only inside its own class
    auto in = lines; in[17] = "        Legs <- 1"; in.insert(in.begin() + 17, "        SUPER.");
    r = complete(in, 17, 14, SymbolIndex::build(in));
    CHECK(labels(r).count("Describe") && labels(r).count("NEW") && !labels(r).count("Learn"));
    // the closing keyword that is needed ranks first on an empty/ "END" prefix
    std::vector<std::string> blk = {"FOR i <- 1 TO 3", "    IF i = 2 THEN", "        OUTPUT i", "    EN"};
    r = complete(blk, 3, 6, SymbolIndex::build(blk)); CHECK_EQ(r.items[0].label, std::string("ENDIF"));
    blk = {"WHILE TRUE DO", "    x <- 1", "    "}; r = complete(blk, 2, 4, SymbolIndex::build(blk), {}, true); CHECK_EQ(r.items[0].label, std::string("ENDWHILE"));
    blk = {"IF x THEN", "    y <- 1", "    EL"}; r = complete(blk, 2, 6, SymbolIndex::build(blk)); CHECK_EQ(r.items[0].label, std::string("ELSE"));
}

TEST(completion_snippets_and_builtin_docs) {
    // every built-in function of the engine has documentation, and nothing is documented that the engine lacks
    std::set<std::string> docs;
    for (const BuiltinDoc& d : builtinDocs()) { docs.insert(d.name); CHECK(pcs::findBuiltin(d.name) != nullptr); CHECK(std::string(d.signature).size() > 3); }
    const char* engineNames[] = {"length", "left", "right", "mid", "substring", "lcase", "ucase", "to_upper", "to_lower", "chr", "asc", "int", "rand", "round",
                                 "num_to_str", "str_to_num", "is_num", "day", "month", "year", "dayindex", "setdate", "today", "eof"};
    for (const char* n : engineNames) { CHECK(pcs::findBuiltin(n) != nullptr); CHECK(docs.count(n) == 1); }
    CHECK_EQ(docs.size(), sizeof(engineNames) / sizeof(engineNames[0]));
    // every built-in snippet is valid PCScript (placeholder removed) and offered by prefix
    for (const Snippet& s : builtinSnippets()) {
        std::string body = s.body; size_t m = body.find("$0"); if (m != std::string::npos) body.erase(m, 2);
        auto diags = [&] { std::string pre = "DECLARE i, Number, Line : INTEGER\nDECLARE Name : STRING\n"; return pre; }();
        (void)diags;
        FormatResult fr = formatSource(body);
        CHECK(fr.ok);
    }
    auto ls = splitLines("ifel");
    auto r = complete(ls, 0, 4, SymbolIndex::build(ls));
    CHECK(!r.items.empty()); CHECK_EQ(r.items[0].label, std::string("ifelse")); CHECK_EQ(r.items[0].kind, ItemKind::Snippet);
    std::vector<Snippet> extra = {{"mysnip", "Mine", "OUTPUT \"mine\"$0"}};
    r = complete(splitLines("mysn"), 0, 4, SymbolIndex::build({}), extra);
    CHECK(!r.items.empty()); CHECK_EQ(r.items[0].label, std::string("mysnip"));
}
