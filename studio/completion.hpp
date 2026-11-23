// Autocomplete: the suggestion drop-down. Context aware (types after ':', classes after NEW, members after '.',
// file modes after OPENFILE ... FOR, the END keyword that closes the innermost open block, ...).
#pragma once
#include <string>
#include <vector>
#include "symbols.hpp"

namespace studio {

enum class ItemKind { Keyword, Type, Builtin, Variable, Constant, Parameter, Procedure, Function, Class, Record, Enum,
                      EnumValue, Field, Method, Snippet };

struct Snippet {
    std::string prefix;        // what the user types to find it ("ifelse")
    std::string description;
    std::string body;          // may contain newlines; "$0" marks where the caret ends up; no other placeholders
};

struct CompletionItem {
    std::string label;         // shown in the list
    std::string match;         // text the typed prefix is matched against
    std::string insert;        // text to insert; may contain "$0"
    std::string detail;        // signature / type, shown beside the label
    std::string doc;           // one-line explanation shown in the details strip
    ItemKind kind = ItemKind::Keyword;
    int score = 0;
};

struct CompletionResult {
    int replaceStart = 0;      // byte column where the typed prefix starts
    int replaceEnd = 0;        // byte column of the caret
    bool memberAccess = false; // the list is for "something."
    std::vector<CompletionItem> items;   // best match first
};

const std::vector<Snippet>& builtinSnippets();

// Signature and one-line description of a built-in function ("LEFT(s : STRING, n : INTEGER) RETURNS STRING").
struct BuiltinDoc { const char* name; const char* signature; const char* doc; };
const std::vector<BuiltinDoc>& builtinDocs();
const BuiltinDoc* findBuiltinDoc(const std::string& lowerName);

// Computes the suggestions at (line, col). `forced` is true for Ctrl+Space (an empty prefix is then allowed).
CompletionResult complete(const std::vector<std::string>& lines, int line, int col, const SymbolIndex& index,
                          const std::vector<Snippet>& extraSnippets = {}, bool forced = false);

// True if typing the character c at this point should open the drop-down on its own (identifier character or '.').
bool isCompletionTrigger(char c);

} // namespace studio
