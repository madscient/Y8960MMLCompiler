// Everything y8mmlc prints in its own words comes from a string literal under
// src/, and those words are English and ASCII so that no console can garble
// them. This looks at every part of src/ that is not a comment - comments may
// be written in any language. What a message quotes from the user - a path, a
// piece of the source - is not in src/ and stays UTF-8.
//
// The scan knows ordinary string and character literals and both comment
// forms. It does not know raw string literals; src/ has none, and one would
// be read as code, which errs towards reporting rather than missing.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "testutil.h"

namespace fs = std::filesystem;

namespace {

// Reports each line that has a byte above 0x7F outside a comment.
void scan(const fs::path& file, const std::string& shown) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string s = buf.str();

    enum class State { Code, LineComment, BlockComment, String, Char } st = State::Code;
    int line = 1;
    int reported = 0;  // the last line reported, so a line is named once
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char next = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (c == '\n') {
            ++line;
            if (st == State::LineComment) st = State::Code;
            continue;
        }
        switch (st) {
            case State::Code:
                if (c == '/' && next == '/') { st = State::LineComment; ++i; continue; }
                if (c == '/' && next == '*') { st = State::BlockComment; ++i; continue; }
                if (c == '"') { st = State::String; continue; }
                if (c == '\'') { st = State::Char; continue; }
                break;
            case State::String:
            case State::Char:
                if (c == '\\') { ++i; continue; }
                if ((st == State::String && c == '"') || (st == State::Char && c == '\'')) {
                    st = State::Code;
                    continue;
                }
                break;
            case State::BlockComment:
                if (c == '*' && next == '/') { st = State::Code; ++i; }
                continue;
            case State::LineComment:
                continue;
        }
        if (static_cast<unsigned char>(c) > 0x7F && reported != line) {
            reported = line;
            test::check(false, shown + ":" + std::to_string(line) +
                                   " has a character that is not ASCII outside a comment");
        }
    }
}

} // namespace

int main() {
    const fs::path root = fs::u8path(Y8MMLC_SOURCE_DIR) / "src";
    test::check(fs::is_directory(root), "src/ is there");

    int files = 0;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(root)) {
        const std::string ext = e.path().extension().string();
        if (!e.is_regular_file() || (ext != ".cpp" && ext != ".h")) continue;
        ++files;
        scan(e.path(), fs::relative(e.path(), root.parent_path()).generic_u8string());
    }
    test::check(files > 0, "src/ has sources to look at");
    return test::report("ascii_test");
}
