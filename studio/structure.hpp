// Block structure of PCScript source (IF/ENDIF, FOR/NEXT, ...), tracked line by line. Tolerant of unfinished code.
// Drives the pretty printer, auto-indent, auto-closing of blocks and "which END keyword do I need" completion.
#pragma once
#include <string>
#include <vector>
#include "scan.hpp"

namespace studio {

enum class BlockKind { If, Case, For, While, Repeat, Procedure, Function, Type, Class };

struct OpenBlock {
    BlockKind kind;
    int line = 0;
    bool inBranch = false;    // CASE only: a branch label has been seen, so statements sit one level deeper
    std::string loopVar;      // FOR only
};

const char* closerKeyword(BlockKind k);   // "ENDIF", "NEXT", "UNTIL", ...

enum class LineClass { Blank, CommentOnly, Code };

struct LineResult {
    int indent = 0;                 // indentation level (in units) this line should have
    LineClass cls = LineClass::Blank;
    bool opens = false;             // the line opened a block
    bool closes = false;            // the line closed a block (or is ELSE / a CASE label that re-levels)
};

class BlockTracker {
public:
    // Processes one line (tokens from scanLine) and updates the open-block stack.
    LineResult feed(const std::vector<LTok>& toks, int lineNumber);
    const std::vector<OpenBlock>& open() const { return stack_; }
    int depth() const;              // indentation level for an ordinary statement at this point
private:
    std::vector<OpenBlock> stack_;
    bool closeBlock(BlockKind k);
    int depthBelowTop() const;
};

// Convenience: run the tracker over lines [0, upTo) and return its state.
BlockTracker trackerBefore(const std::vector<std::string>& lines, int upTo);
// Indentation level of line `index` given the lines above it.
int indentLevelOf(const std::vector<std::string>& lines, int index);
// Opener kind and (for a FOR) loop variable if the line opens a block; used by "auto close blocks".
bool lineOpensBlock(const std::string& line, BlockKind& kind, std::string& loopVar, bool& headerComplete);

} // namespace studio
