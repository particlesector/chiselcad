#include "SourceLoader.h"
#include "Lexer.h"
#include "Parser.h"
#include <algorithm>
#include <fstream>
#include <iterator>
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

    // `isRoot` distinguishes "the file the caller asked to load" from a file
    // reached via include/use, both for the diagnostic wording and so the
    // root-missing diagnostic carries a non-empty filePath (see loadSource —
    // DiagnosticsPanel treats an empty filePath+loc as a silent runtime
    // warning, not a clickable error, which is wrong for "file not found").
    ParseResult loadFile(const std::filesystem::path& path, bool isRoot,
                          SourceLoc refLoc = {}, const std::string& refFile = "") {
        std::error_code ec;
        std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        std::filesystem::path key = ec ? path : canonical;

        for (const auto& active : m_activeStack) {
            if (active == key) {
                addError("circular include: " + path.string(), refLoc, refFile);
                return {};
            }
        }

        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) {
            addError((isRoot ? "cannot open file: " : "cannot open included file: ") + path.string(),
                      refLoc, refFile);
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

        m_activeStack.push_back(std::move(key));
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

    // $fn/$fs/$fa: the including file's own *explicit* setting always wins
    // (globalFnSet/etc. — not just "differs from the language default", which
    // can't tell "explicitly set to the default value" apart from "never
    // set"). An included file's setting only applies where the includer left
    // it unset. Parser flattens statement order away within a file (see
    // AST.h), so this can't honor exact textual position beyond that.
    static void mergeGlobalQuality(ParseResult& into, const ParseResult& from) {
        if (!into.globalFnSet && from.globalFnSet) { into.globalFn = from.globalFn; into.globalFnSet = true; }
        if (!into.globalFsSet && from.globalFsSet) { into.globalFs = from.globalFs; into.globalFsSet = true; }
        if (!into.globalFaSet && from.globalFaSet) { into.globalFa = from.globalFa; into.globalFaSet = true; }
    }

    // Splices `child`'s vectors into `result`'s at index `pos` (clamped to
    // the vector's current size — earlier splices in the same file can only
    // have grown it), moving each element rather than copying.
    template <typename T>
    static void spliceAt(std::vector<T>& dst, std::vector<T>&& src, size_t pos) {
        pos = std::min(pos, dst.size());
        dst.insert(dst.begin() + static_cast<std::ptrdiff_t>(pos),
                   std::make_move_iterator(src.begin()),
                   std::make_move_iterator(src.end()));
    }

    // Resolves every include<>/use<> directive recorded in `result` (which
    // was just parsed from `selfPath`), splicing each target's content in at
    // the directive's recorded position rather than appending at the end, so
    // that e.g. a reassignment after the include still overrides a value the
    // included file set (textual-paste fidelity, within what the flattened
    // per-file vectors can represent — see mergeGlobalQuality above).
    void resolveIncludes(ParseResult& result, const std::filesystem::path& selfPath) {
        std::vector<IncludeStmt> includes = std::move(result.includes);
        result.includes.clear();

        const std::string selfPathStr = selfPath.string();
        const std::filesystem::path baseDir = selfPath.parent_path();

        // How many items earlier includes in this same file have already
        // inserted into each category vector — added to each subsequent
        // include's recorded (pre-splice) index so it lands in the right
        // place in the now-growing vector.
        size_t rootsOffset = 0, assignOffset = 0, moduleOffset = 0, functionOffset = 0;

        for (const auto& inc : includes) {
            std::filesystem::path childPath = baseDir / inc.path;
            ParseResult child = loadFile(childPath, /*isRoot=*/false, inc.loc, selfPathStr);

            // Sizes must be captured before spliceAt moves out of `child` —
            // a moved-from vector's size is unspecified, not guaranteed 0.
            const size_t nModules   = child.moduleDefs.size();
            const size_t nFunctions = child.functionDefs.size();
            const size_t nRoots     = child.roots.size();
            const size_t nAssigns   = child.assignments.size();

            spliceAt(result.moduleDefs,   std::move(child.moduleDefs),   inc.moduleIndex   + moduleOffset);
            spliceAt(result.functionDefs, std::move(child.functionDefs), inc.functionIndex + functionOffset);
            moduleOffset   += nModules;
            functionOffset += nFunctions;

            if (inc.kind == IncludeStmt::Kind::Include) {
                spliceAt(result.roots,       std::move(child.roots),       inc.rootsIndex  + rootsOffset);
                spliceAt(result.assignments, std::move(child.assignments), inc.assignIndex + assignOffset);
                rootsOffset  += nRoots;
                assignOffset += nAssigns;

                mergeGlobalQuality(result, child);
            }
            // Kind::Use: only moduleDefs/functionDefs cross the boundary,
            // already spliced above.
        }
    }
};

} // namespace

LoadedSource loadSource(const std::filesystem::path& rootPath) {
    Loader loader;
    LoadedSource out;
    out.result      = loader.loadFile(rootPath, /*isRoot=*/true, {}, rootPath.string());
    out.diagnostics = std::move(loader.diagnostics);
    return out;
}

} // namespace chisel::lang
