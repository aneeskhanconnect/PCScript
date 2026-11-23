// Symbol index built from a tolerant scan of the source: variables, constants, routines, classes, records, enumerations
// and their members. Used by autocomplete, the formatter and the outline.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace studio {

enum class SymKind { Variable, Constant, Parameter, Procedure, Function, Class, Record, Enum, EnumValue, Pointer, Alias, Field, Method };

struct Symbol {
    std::string name;
    SymKind kind = SymKind::Variable;
    std::string type;        // declared type text ("INTEGER", "ARRAY[1:10] OF INTEGER", "Dog"); return type for functions
    std::string signature;   // routines: "Area(R : REAL) RETURNS REAL"
    std::string owner;       // class/record/enumeration (members, enum values) or routine (parameters, locals)
    std::string parent;      // classes: parent class name
    bool isPrivate = false;
    bool byRef = false;
    int line = 0;            // 0-based line of the declaration
    int scopeStart = -1, scopeEnd = -1;   // lines where a local/parameter is visible, or the extent of a class/routine/type
};

class SymbolIndex {
public:
    static SymbolIndex build(const std::vector<std::string>& lines);

    const std::vector<Symbol>& all() const { return syms_; }
    // Symbols that may be written without qualification at `line` (innermost scope first, shadowed names removed).
    std::vector<const Symbol*> visibleAt(int line) const;
    const Symbol* findType(const std::string& name) const;   // class, record, enumeration, pointer or alias (case-insensitive)
    const Symbol* findRoutine(const std::string& name) const;   // global procedure/function
    // Fields and methods of a class (own first, then inherited) or fields of a record. Private ones are included.
    std::vector<const Symbol*> membersOf(const std::string& typeName) const;
    const Symbol* findMember(const std::string& typeName, const std::string& member) const;
    const Symbol* enclosingClass(int line) const;
    const Symbol* enclosingRoutine(int line) const;
    // Type name of an expression such as "Team[2].Leader" or "SUPER" (text before a dot); "" when unknown.
    std::string typeOfPath(const std::string& path, int line) const;
    std::set<std::string> userNamesLower() const;
    // All declarations that belong in an outline (classes, records, routines, global variables).
    std::vector<const Symbol*> outline() const;

private:
    std::vector<Symbol> syms_;
    int lastLine_ = 0;
    std::string resolveAlias(const std::string& typeText, int depth = 0) const;
    std::string elementType(const std::string& typeText) const;
};

const char* symKindName(SymKind k);

} // namespace studio
