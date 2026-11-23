#include <cctype>
#include "completion.hpp"
#include <algorithm>
#include <set>
#include "scan.hpp"
#include "structure.hpp"

namespace studio {

const std::vector<BuiltinDoc>& builtinDocs() {
    static const std::vector<BuiltinDoc> d = {
        {"length", "LENGTH(s : STRING) RETURNS INTEGER", "Number of characters in s."},
        {"left", "LEFT(s : STRING, n : INTEGER) RETURNS STRING", "The first n characters of s."},
        {"right", "RIGHT(s : STRING, n : INTEGER) RETURNS STRING", "The last n characters of s."},
        {"mid", "MID(s : STRING, start : INTEGER, length : INTEGER) RETURNS STRING", "length characters of s from position start (first position is 1)."},
        {"substring", "SUBSTRING(s : STRING, start : INTEGER, length : INTEGER) RETURNS STRING", "Same as MID."},
        {"lcase", "LCASE(x) RETURNS same kind as x", "Lower-case version of a CHAR or STRING."},
        {"ucase", "UCASE(x) RETURNS same kind as x", "Upper-case version of a CHAR or STRING."},
        {"to_upper", "TO_UPPER(x) RETURNS same kind as x", "Upper-case version of a CHAR or STRING."},
        {"to_lower", "TO_LOWER(x) RETURNS same kind as x", "Lower-case version of a CHAR or STRING."},
        {"chr", "CHR(n : INTEGER) RETURNS CHAR", "The character with ASCII code n."},
        {"asc", "ASC(c : CHAR) RETURNS INTEGER", "ASCII code of c."},
        {"int", "INT(x : REAL) RETURNS INTEGER", "x with the fractional part removed (truncates towards zero)."},
        {"rand", "RAND(x : INTEGER) RETURNS REAL", "Random REAL r with 0 <= r < x."},
        {"round", "ROUND(x : REAL, places : INTEGER) RETURNS REAL", "x rounded to the given number of decimal places."},
        {"num_to_str", "NUM_TO_STR(n) RETURNS STRING", "Converts a number to text."},
        {"str_to_num", "STR_TO_NUM(s : STRING) RETURNS INTEGER or REAL", "Converts text to a number."},
        {"is_num", "IS_NUM(s : STRING) RETURNS BOOLEAN", "TRUE if s can be converted to a number."},
        {"day", "DAY(d : DATE) RETURNS INTEGER", "Day of the month of d."},
        {"month", "MONTH(d : DATE) RETURNS INTEGER", "Month number of d."},
        {"year", "YEAR(d : DATE) RETURNS INTEGER", "Year of d."},
        {"dayindex", "DAYINDEX(d : DATE) RETURNS INTEGER", "Day of the week of d: Sunday = 1 ... Saturday = 7."},
        {"setdate", "SETDATE(day : INTEGER, month : INTEGER, year : INTEGER) RETURNS DATE", "Creates a DATE."},
        {"today", "TODAY() RETURNS DATE", "Today's date."},
        {"eof", "EOF(file : STRING) RETURNS BOOLEAN", "TRUE when the open file has no more lines to read."},
    };
    return d;
}

const BuiltinDoc* findBuiltinDoc(const std::string& lowerName) {
    for (const BuiltinDoc& d : builtinDocs()) if (lowerName == d.name) return &d;
    return nullptr;
}

const std::vector<Snippet>& builtinSnippets() {
    static const std::vector<Snippet> s = {
        {"if", "IF ... THEN ... ENDIF", "IF $0 THEN\n    \nENDIF"},
        {"ifelse", "IF ... THEN ... ELSE ... ENDIF", "IF $0 THEN\n    \nELSE\n    \nENDIF"},
        {"for", "FOR loop", "FOR i <- 1 TO $0\n    \nNEXT i"},
        {"forstep", "FOR loop with STEP", "FOR i <- 10 TO 1 STEP -1\n    $0\nNEXT i"},
        {"while", "WHILE loop", "WHILE $0 DO\n    \nENDWHILE"},
        {"repeat", "REPEAT ... UNTIL loop", "REPEAT\n    \nUNTIL $0"},
        {"case", "CASE OF with OTHERWISE", "CASE OF $0\n    1 : \n    2 : \n    OTHERWISE : \nENDCASE"},
        {"procedure", "PROCEDURE", "PROCEDURE $0()\n    \nENDPROCEDURE"},
        {"function", "FUNCTION", "FUNCTION $0() RETURNS INTEGER\n    RETURN \nENDFUNCTION"},
        {"class", "CLASS with constructor", "CLASS $0\n    PRIVATE Name : STRING\n    PUBLIC PROCEDURE NEW(GivenName : STRING)\n        Name <- GivenName\n    ENDPROCEDURE\nENDCLASS"},
        {"record", "TYPE (record)", "TYPE $0\n    DECLARE Name : STRING\nENDTYPE"},
        {"enum", "TYPE (enumeration)", "TYPE $0 = (First, Second, Third)"},
        {"array", "Array declaration", "DECLARE $0 : ARRAY[1:10] OF INTEGER"},
        {"array2d", "2D array declaration", "DECLARE $0 : ARRAY[1:3, 1:3] OF INTEGER"},
        {"askint", "Prompt for an INTEGER", "DECLARE Number : INTEGER\nOUTPUT \"Enter a number: \"\nINPUT Number$0"},
        {"validate", "Input validation loop", "REPEAT\n    OUTPUT \"Enter a value: \"\n    INPUT $0\nUNTIL TRUE"},
        {"readall", "Read every line of a file", "OPENFILE \"data.txt\" FOR READ\nWHILE NOT EOF(\"data.txt\") DO\n    READFILE \"data.txt\", Line$0\n    OUTPUT Line\nENDWHILE\nCLOSEFILE \"data.txt\""},
        {"writefile", "Write lines to a file", "OPENFILE \"data.txt\" FOR WRITE\nWRITEFILE \"data.txt\", $0\nCLOSEFILE \"data.txt\""},
        {"swap", "Swap procedure (BYREF)", "PROCEDURE Swap(BYREF A : INTEGER, BYREF B : INTEGER)\n    DECLARE Temp : INTEGER\n    Temp <- A\n    A <- B\n    B <- Temp$0\nENDPROCEDURE"},
        {"hello", "Hello World", "OUTPUT \"Hello, World!\"$0"},
    };
    return s;
}

bool isCompletionTrigger(char c) { return isWordChar(c) || c == '.'; }

namespace {

struct KwInfo { const char* word; const char* doc; bool stmt; bool expr; };

const std::vector<KwInfo>& keywords() {
    static const std::vector<KwInfo> k = {
        {"DECLARE", "DECLARE name : TYPE  - create a variable", true, false},
        {"CONSTANT", "CONSTANT name <- value  - a value that cannot change", true, false},
        {"INPUT", "INPUT variable  - read a line typed by the user", true, false},
        {"OUTPUT", "OUTPUT a, b, ...  - print values and end the line", true, false},
        {"IF", "IF condition THEN ... [ELSE ...] ENDIF", true, false},
        {"THEN", "Follows the condition of an IF", false, true},
        {"ELSE", "The alternative branch of an IF", true, false},
        {"ENDIF", "Closes an IF", true, false},
        {"CASE", "CASE OF variable ... ENDCASE  - choose between many values", true, false},
        {"OF", "Used in CASE OF and ARRAY ... OF", false, true},
        {"OTHERWISE", "The default branch of a CASE", true, false},
        {"ENDCASE", "Closes a CASE", true, false},
        {"FOR", "FOR i <- a TO b [STEP s] ... NEXT i", true, false},
        {"TO", "Upper bound of a FOR loop or range in CASE", false, true},
        {"STEP", "Step size of a FOR loop", false, true},
        {"NEXT", "Closes a FOR loop", true, false},
        {"WHILE", "WHILE condition DO ... ENDWHILE", true, false},
        {"DO", "Follows the condition of a WHILE", false, true},
        {"ENDWHILE", "Closes a WHILE loop", true, false},
        {"REPEAT", "REPEAT ... UNTIL condition", true, false},
        {"UNTIL", "Closes a REPEAT loop; the loop ends when the condition is TRUE", true, false},
        {"PROCEDURE", "PROCEDURE Name(parameters) ... ENDPROCEDURE", true, false},
        {"ENDPROCEDURE", "Closes a PROCEDURE", true, false},
        {"FUNCTION", "FUNCTION Name(parameters) RETURNS TYPE ... ENDFUNCTION", true, false},
        {"ENDFUNCTION", "Closes a FUNCTION", true, false},
        {"RETURNS", "The result type of a FUNCTION", false, false},
        {"RETURN", "RETURN value  - leave a function with its result", true, false},
        {"CALL", "CALL Procedure(arguments)", true, false},
        {"BYVAL", "Parameter passed by value (a copy)", false, false},
        {"BYREF", "Parameter passed by reference (changes the caller's variable)", false, false},
        {"TYPE", "TYPE Name ... ENDTYPE (record) or TYPE Name = (a, b) (enumeration)", true, false},
        {"ENDTYPE", "Closes a record TYPE", true, false},
        {"CLASS", "CLASS Name [INHERITS Parent] ... ENDCLASS", true, false},
        {"ENDCLASS", "Closes a CLASS", true, false},
        {"PUBLIC", "Member that any code may use", true, false},
        {"PRIVATE", "Member only the class itself may use", true, false},
        {"INHERITS", "CLASS Child INHERITS Parent", false, false},
        {"NEW", "NEW ClassName(arguments)  - create an object", false, true},
        {"SUPER", "SUPER.Method(...)  - call the parent class", true, true},
        {"OPENFILE", "OPENFILE \"name\" FOR READ | WRITE | APPEND | RANDOM", true, false},
        {"CLOSEFILE", "CLOSEFILE \"name\"", true, false},
        {"READFILE", "READFILE \"name\", variable  - read one line", true, false},
        {"WRITEFILE", "WRITEFILE \"name\", value  - write one line", true, false},
        {"SEEK", "SEEK \"name\", recordNumber  - random-access files", true, false},
        {"GETRECORD", "GETRECORD \"name\", record", true, false},
        {"PUTRECORD", "PUTRECORD \"name\", record", true, false},
        {"EOF", "EOF(\"name\")  - TRUE at the end of a file", false, true},
        {"ARRAY", "ARRAY[lower:upper] OF TYPE", false, false},
        {"AND", "Logical AND", false, true}, {"OR", "Logical OR", false, true}, {"NOT", "Logical NOT", false, true},
        {"MOD", "Remainder of integer division", false, true}, {"DIV", "Integer division", false, true},
        {"TRUE", "BOOLEAN value", false, true}, {"FALSE", "BOOLEAN value", false, true},
    };
    return k;
}

ItemKind kindOf(SymKind k) {
    switch (k) {
        case SymKind::Variable: return ItemKind::Variable; case SymKind::Constant: return ItemKind::Constant;
        case SymKind::Parameter: return ItemKind::Parameter; case SymKind::Procedure: return ItemKind::Procedure;
        case SymKind::Function: return ItemKind::Function; case SymKind::Class: return ItemKind::Class;
        case SymKind::Record: case SymKind::Pointer: case SymKind::Alias: return ItemKind::Record;
        case SymKind::Enum: return ItemKind::Enum; case SymKind::EnumValue: return ItemKind::EnumValue;
        case SymKind::Field: return ItemKind::Field; case SymKind::Method: return ItemKind::Method;
    }
    return ItemKind::Variable;
}

// How well does `typed` match `cand`?  0 = no match.
int matchScore(const std::string& typed, const std::string& cand) {
    if (typed.empty()) return 10;
    if (cand.compare(0, typed.size(), typed) == 0) return cand.size() == typed.size() ? 120 : 100;
    std::string lt = toLower(typed), lc = toLower(cand);
    if (lc.compare(0, lt.size(), lt) == 0) return lc.size() == lt.size() ? 115 : 90;
    // initials of words / camelCase / underscores:  nts -> NUM_TO_STR, gn -> GetName
    std::string initials;
    for (size_t i = 0; i < cand.size(); ++i) {
        bool boundary = i == 0 || cand[i - 1] == '_' || (std::isupper(static_cast<unsigned char>(cand[i])) && std::islower(static_cast<unsigned char>(cand[i - 1])));
        if (boundary && cand[i] != '_') initials += static_cast<char>(std::tolower(static_cast<unsigned char>(cand[i])));
    }
    if (initials.compare(0, lt.size(), lt) == 0) return 70;
    if (lc.find(lt) != std::string::npos) return 55;
    // subsequence
    size_t j = 0;
    for (size_t i = 0; i < lc.size() && j < lt.size(); ++i) if (lc[i] == lt[j]) ++j;
    if (j == lt.size() && lt.size() >= 2) return 30;
    return 0;
}

std::string callInsert(const std::string& name, const std::string& sig) {
    // zero-argument routines get "()" with the caret after it; others get "(" caret ")"
    size_t o = sig.find('(');
    bool noArgs = o != std::string::npos && o + 1 < sig.size() && sig[o + 1] == ')';
    return noArgs ? name + "()$0" : name + "($0)";
}

enum class Ctx { Statement, Expression, Type, ClassName, FileMode, CallTarget, LoopVar, None };

} // namespace

CompletionResult complete(const std::vector<std::string>& lines, int line, int col, const SymbolIndex& index,
                          const std::vector<Snippet>& extra, bool forced) {
    CompletionResult res;
    if (line < 0 || line >= static_cast<int>(lines.size())) return res;
    const std::string& text = lines[static_cast<size_t>(line)];
    col = std::max(0, std::min(col, static_cast<int>(text.size())));
    const std::string before = text.substr(0, static_cast<size_t>(col));

    // Inside a comment or string literal: nothing to suggest.
    std::vector<LTok> bt = scanLine(before);
    if (!bt.empty()) {
        const LTok& last = bt.back();
        if (last.kind == Tk::Comment) return res;
        if ((last.kind == Tk::String || last.kind == Tk::Char) && !last.terminated) return res;
    }

    int ws = col;
    while (ws > 0 && isWordChar(before[static_cast<size_t>(ws - 1)])) --ws;
    const std::string typed = before.substr(static_cast<size_t>(ws));
    res.replaceStart = ws; res.replaceEnd = col;
    if (!typed.empty() && std::isdigit(static_cast<unsigned char>(typed[0]))) return res;   // a number

    std::vector<CompletionItem> items;
    auto add = [&](CompletionItem it, int bonus) {
        it.score = matchScore(typed, it.match.empty() ? it.label : it.match);
        if (it.score == 0) return;
        it.score += bonus;
        items.push_back(std::move(it));
    };

    // ---- member access:  expr.  ----
    if (ws > 0 && before[static_cast<size_t>(ws - 1)] == '.') {
        res.memberAccess = true;
        int e = ws - 1, s = e, depth = 0;
        while (s > 0) {
            char c = before[static_cast<size_t>(s - 1)];
            if (c == ']' || c == ')') ++depth;
            else if (c == '[' || c == '(') { if (depth == 0) break; --depth; }
            else if (depth == 0 && !isWordChar(c) && c != '.' && c != '^') break;
            --s;
        }
        std::string path = before.substr(static_cast<size_t>(s), static_cast<size_t>(e - s));
        std::string type = index.typeOfPath(path, line);
        if (type.empty()) return res;
        const Symbol* inClass = index.enclosingClass(line);
        for (const Symbol* m : index.membersOf(type)) {
            if (m->isPrivate && !(inClass && toLower(inClass->name) == toLower(m->owner))) continue;
            if (m->kind == SymKind::Method && toLower(m->name) == "new" && toUpper(path) != "SUPER") continue;
            CompletionItem it; it.label = m->name; it.kind = kindOf(m->kind); it.detail = m->signature; it.doc = std::string(symKindName(m->kind)) + " of " + m->owner;
            it.insert = m->kind == SymKind::Method ? callInsert(m->name, m->signature) : m->name;
            add(std::move(it), 0);
        }
    } else {
        // ---- classify the position ----
        std::vector<LTok> ptoks = scanLine(before.substr(0, static_cast<size_t>(ws)));
        promoteKeywords(ptoks);
        auto isKwTok = [&](size_t i, const char* w) { return i < ptoks.size() && ptoks[i].kind == Tk::Keyword && ptoks[i].text == w; };
        auto isOpTok = [&](size_t i, const char* w) { return i < ptoks.size() && ptoks[i].kind == Tk::Op && ptoks[i].text == w; };
        size_t n = ptoks.size();
        size_t first = 0;
        if (n && (isKwTok(0, "PUBLIC") || isKwTok(0, "PRIVATE"))) first = 1;
        Ctx cx = Ctx::Expression;
        if (n <= first) cx = Ctx::Statement;
        else {
            const LTok& p = ptoks[n - 1];
            if (isOpTok(n - 1, ":") || isKwTok(n - 1, "RETURNS") || isKwTok(n - 1, "OF") || isOpTok(n - 1, "^") || isKwTok(n - 1, "BYREF") || isKwTok(n - 1, "BYVAL")) {
                // ':' is a type position in DECLARE / parameters / fields, but a CASE label colon starts a statement
                bool declLike = isKwTok(first, "DECLARE") || isKwTok(first, "PROCEDURE") || isKwTok(first, "FUNCTION") || isKwTok(first, "CONSTANT");
                bool paramOrField = false;
                for (size_t i = first; i < n; ++i) if (isOpTok(i, "(")) paramOrField = true;
                bool bareField = first < n && ptoks[first].kind == Tk::Ident;
                if (isOpTok(n - 1, ":")) {
                    const Symbol* ctxClass = index.enclosingClass(line);
                    bool inTypeBlock = false;
                    for (const Symbol& s : index.all()) if (s.kind == SymKind::Record && s.scopeStart <= line && line <= s.scopeEnd) inTypeBlock = true;
                    if (declLike || paramOrField || ((ctxClass || inTypeBlock) && bareField && !index.enclosingRoutine(line))) cx = Ctx::Type;
                    else cx = Ctx::Statement;   // CASE label colon
                } else cx = Ctx::Type;
            }
            else if (isKwTok(n - 1, "NEW") || isKwTok(n - 1, "INHERITS")) cx = Ctx::ClassName;
            else if (isKwTok(n - 1, "FOR") && isKwTok(first, "OPENFILE")) cx = Ctx::FileMode;
            else if (isKwTok(n - 1, "CALL")) cx = Ctx::CallTarget;
            else if (isKwTok(n - 1, "FOR") && isKwTok(first, "FOR")) cx = Ctx::LoopVar;
            else if (isKwTok(first, "DECLARE") || isKwTok(first, "CONSTANT")) cx = (p.kind == Tk::Op && p.text == ",") ? Ctx::None : Ctx::Expression;
            else if ((isKwTok(first, "PROCEDURE") || isKwTok(first, "FUNCTION") || isKwTok(first, "CLASS") || isKwTok(first, "TYPE")) &&
                     !isOpTok(n - 1, "(") && !isOpTok(n - 1, ",")) cx = Ctx::None;   // naming a new routine/class: nothing to suggest
            else cx = Ctx::Expression;
        }
        if (cx == Ctx::None && !forced) return res;
        if (typed.empty() && !forced && cx != Ctx::Type && cx != Ctx::FileMode && cx != Ctx::ClassName) return res;

        const BlockTracker tracker = trackerBefore(lines, line);
        std::string needed;
        if (!tracker.open().empty()) needed = closerKeyword(tracker.open().back().kind);
        bool innerIf = !tracker.open().empty() && tracker.open().back().kind == BlockKind::If;
        bool innerCase = !tracker.open().empty() && tracker.open().back().kind == BlockKind::Case;

        std::set<std::string> seen;   // lower-case labels already offered
        auto sym = [&](const Symbol* s, int bonus) {
            CompletionItem it; it.label = s->name; it.kind = kindOf(s->kind); it.detail = s->signature.empty() ? s->type : s->signature;
            it.doc = symKindName(s->kind);
            bool callable = s->kind == SymKind::Function || s->kind == SymKind::Method || s->kind == SymKind::Procedure;
            it.insert = callable ? callInsert(s->name, s->signature) : s->name;
            if (!seen.insert(toLower(s->name)).second) return;
            add(std::move(it), bonus);
        };
        auto kw = [&](const KwInfo& k, int bonus) {
            CompletionItem it; it.label = k.word; it.insert = k.word; it.kind = ItemKind::Keyword; it.doc = k.doc;
            if (!seen.insert(toLower(k.word)).second) return;
            add(std::move(it), bonus);
        };
        auto typeWords = [&](int bonus) {
            for (const char* t : {"INTEGER", "REAL", "STRING", "CHAR", "BOOLEAN", "DATE", "ARRAY"}) {
                CompletionItem it; it.label = t; it.insert = (std::string(t) == "ARRAY") ? "ARRAY[1:$0] OF INTEGER" : t;
                it.kind = ItemKind::Type; it.doc = std::string(t) == "ARRAY" ? "ARRAY[lower:upper] OF TYPE" : "Built-in type";
                if (!seen.insert(toLower(t)).second) continue;
                add(std::move(it), bonus);
            }
            for (const Symbol& s : index.all())
                if (s.kind == SymKind::Class || s.kind == SymKind::Record || s.kind == SymKind::Enum || s.kind == SymKind::Pointer || s.kind == SymKind::Alias) sym(&s, 5);
        };

        switch (cx) {
            case Ctx::Type: typeWords(10); break;
            case Ctx::ClassName:
                for (const Symbol& s : index.all()) if (s.kind == SymKind::Class) sym(&s, 10);
                break;
            case Ctx::FileMode:
                for (const char* m : {"READ", "WRITE", "APPEND", "RANDOM"}) {
                    CompletionItem it; it.label = m; it.insert = m; it.kind = ItemKind::Keyword;
                    it.doc = std::string(m) == "READ" ? "Read lines from an existing file" : std::string(m) == "WRITE" ? "Create or overwrite a file" :
                             std::string(m) == "APPEND" ? "Add lines to the end of a file" : "Random-access records (SEEK / GETRECORD / PUTRECORD)";
                    add(std::move(it), 10);
                }
                break;
            case Ctx::CallTarget:
                for (const Symbol* s : index.visibleAt(line))
                    if (s->kind == SymKind::Procedure || s->kind == SymKind::Method) sym(s, 10);
                for (const Symbol* s : index.visibleAt(line))
                    if (s->kind == SymKind::Variable || s->kind == SymKind::Parameter || s->kind == SymKind::Field) sym(s, 0);
                kw(KwInfo{"SUPER", "SUPER.Method(...)  - call the parent class", true, true}, 0);
                break;
            case Ctx::LoopVar:
                for (const Symbol* s : index.visibleAt(line))
                    if ((s->kind == SymKind::Variable || s->kind == SymKind::Parameter) && toUpper(s->type) == "INTEGER") sym(s, 15);
                break;
            case Ctx::None: break;
            case Ctx::Statement: case Ctx::Expression: {
                bool stmt = cx == Ctx::Statement;
                for (const Symbol* s : index.visibleAt(line)) {
                    bool local = s->kind == SymKind::Parameter || (!s->owner.empty() && (s->kind == SymKind::Variable || s->kind == SymKind::Constant));
                    bool isType = s->kind == SymKind::Class || s->kind == SymKind::Record || s->kind == SymKind::Enum || s->kind == SymKind::Pointer || s->kind == SymKind::Alias;
                    if (isType) continue;       // type names are offered after ':' / NEW only
                    if (!stmt && s->kind == SymKind::Procedure) continue;
                    sym(s, local ? 12 : 6);
                }
                if (!stmt) for (const BuiltinDoc& b : builtinDocs()) {
                    if (toLower(b.name) == "eof") continue;   // EOF is a keyword
                    CompletionItem it; it.label = toUpper(b.name); it.insert = callInsert(toUpper(b.name), b.signature);
                    it.kind = ItemKind::Builtin; it.detail = b.signature; it.doc = b.doc;
                    if (!seen.insert(toLower(b.name)).second) continue;
                    add(std::move(it), 4);
                }
                for (const KwInfo& k : keywords()) {
                    if (stmt ? !k.stmt : !k.expr) continue;
                    int bonus = 0;
                    if (stmt && k.word == needed) bonus += 25;
                    if (stmt && innerIf && std::string(k.word) == "ELSE") bonus += 8;
                    if (stmt && innerCase && std::string(k.word) == "OTHERWISE") bonus += 8;
                    kw(k, bonus);
                }
                if (stmt) {
                    std::vector<Snippet> all = builtinSnippets();
                    all.insert(all.end(), extra.begin(), extra.end());
                    for (const Snippet& sn : all) {
                        CompletionItem it; it.label = sn.prefix; it.match = sn.prefix; it.insert = sn.body; it.kind = ItemKind::Snippet;
                        it.detail = sn.description; it.doc = "Snippet: " + sn.description;
                        add(std::move(it), -3);
                    }
                }
                break;
            }
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const CompletionItem& a, const CompletionItem& b) {
        if (a.score != b.score) return a.score > b.score;
        return toLower(a.label) < toLower(b.label);
    });
    if (items.size() > 80) items.resize(80);
    // Nothing left to complete: the only candidate is exactly what was typed.
    if (items.size() == 1 && !typed.empty() && items[0].insert == typed && !res.memberAccess) items.clear();
    res.items = std::move(items);
    return res;
}

} // namespace studio
