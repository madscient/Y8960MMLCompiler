// The TextMate grammar in syntaxes/ names the meta commands and the device
// symbols in regular expressions of its own. Nothing but this test notices when
// the compiler learns a new one and the grammar does not.

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "device.h"
#include "json.h"
#include "source.h"
#include "testutil.h"

using namespace y8;

namespace {

std::set<std::string> lowerSet(const std::vector<std::string>& v) {
    std::set<std::string> s;
    for (std::string x : v) {
        for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        s.insert(x);
    }
    return s;
}

// The alternatives between `open` and `close` in `regex`, split on '|'.
bool alternatives(const std::string& regex, const std::string& open, const std::string& close,
                  std::vector<std::string>& out) {
    std::size_t a = regex.find(open);
    if (a == std::string::npos) return false;
    a += open.size();
    std::size_t b = regex.find(close, a);
    if (b == std::string::npos) return false;
    std::string body = regex.substr(a, b - a);
    std::size_t start = 0;
    for (;;) {
        std::size_t bar = body.find('|', start);
        out.push_back(body.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    return true;
}

std::string describe(const std::set<std::string>& s) {
    std::string out;
    for (const std::string& x : s) out += (out.empty() ? "" : " ") + x;
    return out;
}

void sameSet(const std::string& what, const std::set<std::string>& grammar,
             const std::set<std::string>& compiler) {
    test::check(grammar == compiler,
                what + ": the grammar has {" + describe(grammar) + "}, the compiler {" +
                    describe(compiler) + "}");
}

} // namespace

int main() {
    std::ifstream in(Y8MMLC_GRAMMAR_PATH, std::ios::binary);
    test::check(static_cast<bool>(in), "the grammar file opens");
    std::ostringstream buf;
    buf << in.rdbuf();

    JsonValue root;
    std::string error;
    test::check(parseJson(buf.str(), root, error), "the grammar is JSON: " + error);

    std::string scope;
    const JsonValue* scopeValue = root.member("scopeName");
    test::check(scopeValue && scopeValue->asString(scope) && scope == "source.y8960mml",
                "the scope name is source.y8960mml");

    const JsonValue* repo = root.member("repository");
    test::check(repo && repo->isObject(), "the grammar has a repository");
    if (!repo) return test::report("grammar_test");

    std::string meta;
    const JsonValue* metaRule = repo->member("meta-command");
    const JsonValue* metaBegin = metaRule ? metaRule->member("begin") : nullptr;
    std::vector<std::string> metaNames;
    test::check(metaBegin && metaBegin->asString(meta) &&
                    alternatives(meta, "(?i:(", "))", metaNames),
                "the meta-command rule keeps its (?i:(...)) group");
    sameSet("meta commands", lowerSet(metaNames), lowerSet(metaCommandNames()));

    std::string dev;
    const JsonValue* devRule = repo->member("device-symbol");
    const JsonValue* devMatch = devRule ? devRule->member("match") : nullptr;
    std::vector<std::string> devNames;
    test::check(devMatch && devMatch->asString(dev) &&
                    alternatives(dev, "\\b(?:", ")\\b", devNames),
                "the device-symbol rule keeps its (?:...) group");
    std::vector<std::string> symbols;
    for (int i = 0; i < kDeviceCount; ++i) symbols.emplace_back(deviceSymbol(static_cast<Device>(i)));
    sameSet("device symbols", lowerSet(devNames), lowerSet(symbols));

    return test::report("grammar_test");
}
