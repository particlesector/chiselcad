#include "SourceLoader.h"
#include "Lexer.h"
#include "Parser.h"
#include <fstream>
#include <system_error>
#include <utility>

namespace chisel::lang {

namespace {

// ---------------------------------------------------------------------------
// Loader — recursion state for one loadSource() call.
// ---------------------------------------------------------------------------
class Loader {
public:
    DiagList diagnostics;

    ParseResult loadFile(const std::filesystem::path& path,
                          SourceLoc refLoc = {}, const std::string& refFile = "") {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        const std::filesystem::path& key = ec ? path : canonical;

        for (const auto& active : m_activeStack) {
            if (active == key) {
                addError("circular include: " + path.string(), refLoc, refFile);
                return {};
            }
        }

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            addError("cannot open included file: " + path.string(), refLoc, refFile);
            return {};
        }
        std::string src((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        f.close();

        const std::string pathStr = path.string();

        Lexer lexer(src, pathStr);
        auto tokens = lexer.tokenize();
        for (const auto& d : lexer.diagnostics()) diagnostics.push_back(d);

        Parser parser(std::move(tokens), pathStr);
        ParseResult result = parser.parse();
        for (const auto& d : parser.diagnostics()) diagnostics.push_back(d);

        m_activeStack.push_back(key);
        resolveIncludes(result, path);
        m_activeStack.pop_back();

        return result;
    }

private:
    std::vector<std::filesystem::path> m_activeStack; // cycle detection

    void addError(std::string msg, SourceLoc loc, std::string filePath) {
        Diagnostic d;
        d.level    = DiagLevel::Error;
        d.message  = std::move(msg);
        d.loc      = loc;
        d.filePath = std::move(filePath);
        diagnostics.push_back(std::move(d));
    }

    static void mergeInclude(ParseResult& into, ParseResult&& from) {
        for (auto& r : from.roots)         into.roots.push_back(std::move(r));
        for (auto& a : from.assignments)   into.assignments.push_back(std::move(a));
        for (auto& m : from.moduleDefs)    into.moduleDefs.push_back(std::move(m));
        for (auto& fn : from.functionDefs) into.functionDefs.push_back(std::move(fn));

        // $fn/$fs/$fa: the including file's own setting always wins. Parser
        // flattens statement order away (see AST.h), so this is a best-effort
        // fallback: an included file's quality setting only applies if the
        // includer left it at the language default.
        if (into.globalFn == 0.0 && from.globalFn != 0.0) into.globalFn = from.globalFn;
        if (into.globalFs == 2.0 && from.globalFs != 2.0) into.globalFs = from.globalFs;
        if (into.globalFa == 12.0 && from.globalFa != 12.0) into.globalFa = from.globalFa;
    }

    static void mergeUse(ParseResult& into, ParseResult&& from) {
        for (auto& m : from.moduleDefs)    into.moduleDefs.push_back(std::move(m));
        for (auto& fn : from.functionDefs) into.functionDefs.push_back(std::move(fn));
    }

    void resolveIncludes(ParseResult& result, const std::filesystem::path& selfPath) {
        std::vector<IncludeStmt> includes = std::move(result.includes);
        result.includes.clear();

        const std::string selfPathStr = selfPath.string();
        const std::filesystem::path baseDir = selfPath.parent_path();

        for (const auto& inc : includes) {
            std::filesystem::path childPath = baseDir / inc.path;
            ParseResult child = loadFile(childPath, inc.loc, selfPathStr);

            if (inc.kind == IncludeStmt::Kind::Include)
                mergeInclude(result, std::move(child));
            else
                mergeUse(result, std::move(child));
        }
    }
};

} // namespace

LoadedSource loadSource(const std::filesystem::path& rootPath) {
    Loader loader;
    LoadedSource out;
    out.result      = loader.loadFile(rootPath);
    out.diagnostics = std::move(loader.diagnostics);
    return out;
}

} // namespace chisel::lang
