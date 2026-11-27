#include <cstdio>
#include <fstream>
#include <set>
#include <filesystem>
#include "core/toolkit.h"
#include "core/profiles.h"
#include "app/process.h"
#include "app/toolbox.h"
using namespace heng;
namespace fs = std::filesystem;
static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void touch(const fs::path& p, const char* body = "x") { fs::create_directories(p.parent_path()); std::ofstream f(p); f << body; }

int main() {
    // catalog
    std::set<std::string> ids; bool anyDecomp = false, anyComp = false;
    for (const ToolDef& d : toolCatalog()) { CHECK(ids.insert(d.id).second); CHECK(!d.exeNames.empty()); CHECK(!d.url.empty()); anyDecomp |= d.decompiles(); anyComp |= d.builds(); }
    CHECK(anyDecomp && anyComp);
    CHECK(findToolDef("ghidra") && findToolDef("nasm") && !findToolDef("nope"));

    // splitArgs
    auto a = splitArgs("-O2 -Wall \"-DNAME=a b\" 'x y'");
    CHECK(a.size() == 4 && a[2] == "-DNAME=a b" && a[3] == "x y");
    CHECK(splitArgs("   ").empty());

    // locating
    fs::path tmp = fs::temp_directory_path() / "he_toolkit_test"; fs::remove_all(tmp);
    touch(tmp / "bin" / "objdump"); touch(tmp / "ghidra_11.0_PUBLIC" / "support" / "analyzeHeadless"); touch(tmp / "ghidra_11.0_PUBLIC" / "x");
    const ToolDef* od = findToolDef("objdump"); const ToolDef* gh = findToolDef("ghidra");
    CHECK(!locateTool(*od, "", {(tmp / "bin").string()}, {}, false).empty());
    CHECK(locateTool(*od, "", {(tmp / "nothing").string()}, {}, false).empty());
    std::string g = locateTool(*gh, "", {}, {tmp.string()}, false);
    CHECK(!g.empty() && g.find("analyzeHeadless") != std::string::npos);
    CHECK(!locateTool(*gh, (tmp / "ghidra_11.0_PUBLIC").string(), {}, {}, false).empty());
    CHECK(!locateTool(*gh, tmp.string(), {}, {}, false).empty());   // one level down
    CHECK(splitPathList("/a:/b::/c", false).size() == 3 && splitPathList("C:\\a;\"C:\\b b\"", true).size() == 2);

    // decompile commands
    DecompileOpts o;
    CmdSpec c = buildDecompileCmd(*gh, "/g/analyzeHeadless", "/in/game.exe", "/w/decomp/game.ghidra", "/w", o);
    CHECK(c.error.empty() && c.outFile == "/w/decomp/game.ghidra.c" && !c.mkdir.empty());
    bool hasImport = false, hasScript = false; for (auto& s : c.argv) { hasImport |= s == "-import"; hasScript |= s == ghidraScriptName(); }
    CHECK(hasImport && hasScript && c.argv.back() == "-deleteProject");
    CHECK(std::string(ghidraScriptSource()).find("class HappyDecompile") != std::string::npos);
    o.function = "main;rm -rf";
    c = buildDecompileCmd(*findToolDef("radare2"), "r2", "/x.exe", "/o", "/w", o);
    CHECK(c.outFile.empty() && c.argv.back() == "/x.exe" && c.argv[c.argv.size() - 2].find("sym.main_rm_-rf") != std::string::npos);
    bool noInject = true; for (auto& s : c.argv) if (s.find("; rm") != std::string::npos || s.find("main;") != std::string::npos) noInject = false; CHECK(noInject);
    c = buildDecompileCmd(*findToolDef("objdump"), "objdump", "/x.exe", "/o", "/w", DecompileOpts());
    CHECK(c.argv[1] == "-d" && c.argv.back() == "/x.exe");
    c = buildDecompileCmd(*findToolDef("gcc"), "gcc", "/x", "/o", "/w", DecompileOpts());
    CHECK(!c.error.empty());

    // compile commands
    CompileOpts co; co.sources = {"a.c"}; co.output = "out.exe"; co.flags = "-O2 -Wall"; co.bits = 32;
    c = buildCompileCmd(*findToolDef("gcc"), "gcc", co);
    CHECK(c.error.empty() && c.argv[1] == "a.c" && c.argv[2] == "-o");
    bool m32 = false; for (auto& s : c.argv) m32 |= s == "-m32"; CHECK(m32);
    c = buildCompileCmd(*findToolDef("mingw32"), "i686-w64-mingw32-gcc", co);
    m32 = false; for (auto& s : c.argv) m32 |= s == "-m32"; CHECK(!m32);
    co.sources = {"a.asm"}; co.windowsHost = true; co.flags.clear();
    c = buildCompileCmd(*findToolDef("nasm"), "nasm", co);
    CHECK(c.argv[2] == "win32");
    co.bits = 64; c = buildCompileCmd(*findToolDef("nasm"), "nasm", co); CHECK(c.argv[2] == "win64");
    co.sources.clear(); CHECK(!buildCompileCmd(*findToolDef("gcc"), "gcc", co).error.empty());
    CHECK(sourceLang("x.CPP") == "cpp" && sourceLang("a.c") == "c" && sourceLang("a.asm") == "asm" && sourceLang("a.txt").empty());
    CHECK(compilerSupports(*findToolDef("gcc"), "c") && !compilerSupports(*findToolDef("gcc"), "cpp") && compilerSupports(*findToolDef("g++"), "cpp") && compilerSupports(*findToolDef("nasm"), "asm"));

    // code blocks
    std::vector<std::string> langs;
    auto blocks = extractCodeBlocks("hi\n```c\nint main(){}\n```\ntext\n```happy-plan\n{}\n```\n```cpp\nint x;\n```", &langs);
    CHECK(blocks.size() == 2 && blocks[0] == "int main(){}" && langs[1] == "cpp");

    // chat helpers
    { std::string cmd; std::vector<std::string> ar;
      CHECK(parseSlash("/decompile \"C:\\My Games\\a.exe\" main", cmd, ar) && cmd == "decompile" && ar.size() == 2 && ar[0] == "C:\\My Games\\a.exe" && ar[1] == "main");
      CHECK(parseSlash("  /TOOLS", cmd, ar) && cmd == "tools" && ar.empty());
      CHECK(!parseSlash("hello /tools", cmd, ar) && !parseSlash("/usr/bin/gcc", cmd, ar));
      auto p = extractPathsFromText("please decompile C:\\Games\\x\\game.exe, and \"D:\\My Stuff\\b.dll\" or /home/me/a.out.");
      CHECK(p.size() == 3 && p[0] == "C:\\Games\\x\\game.exe" && p[1] == "D:\\My Stuff\\b.dll" && p[2] == "/home/me/a.out");
      CHECK(extractPathsFromText("use /tools and 3/4 of the data and http://x.y/z").size() == 0); }

    // config round trip
    AppConfig cfg; cfg.toolPaths["ghidra"] = "/opt/ghidra"; cfg.toolPaths["g++"] = "C:\\mingw\\bin\\g++.exe";
    ApiProfile pr; cfg.profiles.push_back(pr);
    AppConfig back = parseConfig(serializeConfig(cfg, [](const std::string& s) { return s; }), [](const std::string& s) { return s; });
    CHECK(back.toolPaths.size() == 2 && back.toolPaths["g++"] == "C:\\mingw\\bin\\g++.exe");

    // process runner (POSIX only)
#ifndef _WIN32
    he::RunResult r = he::runProcess({"/bin/sh", "-c", "echo hello; echo err 1>&2; exit 3"}, "", 10, nullptr);
    CHECK(r.started && r.exitCode == 3 && r.output.find("hello") != std::string::npos && r.output.find("err") != std::string::npos);
    r = he::runProcess({"/definitely/not/here"}, "", 5, nullptr); CHECK(!r.started && !r.error.empty());
    r = he::runProcess({"/bin/sh", "-c", "sleep 5"}, "", 1, nullptr); CHECK(r.timedOut);
    std::atomic<bool> cancel(false); cancel = true;
    r = he::runProcess({"/bin/sh", "-c", "sleep 5"}, "", 20, &cancel); CHECK(r.cancelled);

    // toolbox end-to-end with a real compiler if present
    he::ToolboxEnv env; env.windows = false; env.workDir = (tmp / "work").string();
    env.pathDirs = splitPathList(std::getenv("PATH") ? std::getenv("PATH") : "", false);
    he::Toolbox tb; tb.configure(env, {}); tb.rescan();
    CHECK(!tb.statuses().empty());
    if (tb.status("gcc").found) {
        fs::path src = tmp / "t.c"; touch(src, "#include <stdio.h>\nint main(){puts(\"hi\");return 0;}\n");
        he::JobResult jr = tb.compile("", {src.string()}, "hello", "-O1", 0, nullptr);
        CHECK(jr.ok && fs::exists(jr.outFile));
        he::RunResult run = he::runProcess({jr.outFile}, "", 10, nullptr); CHECK(run.output == "hi\n");
        touch(src, "int main( { }");
        jr = tb.compile("gcc", {src.string()}, "bad", "", 0, nullptr); CHECK(!jr.ok && !jr.text.empty());
        if (tb.status("objdump").found) {
            he::JobResult dj = tb.decompile("objdump", (tmp / "work/build/hello").string(), "", false, nullptr);
            CHECK(dj.ok && dj.text.find("main") != std::string::npos);
            he::JobResult cached = tb.decompile("objdump", (tmp / "work/build/hello").string(), "", true, nullptr); CHECK(cached.fromCache);
        }
    } else std::printf("(gcc not found: build test skipped)\n");
    he::JobResult none = tb.decompile("ghidra", "/nonexistent", "", false, nullptr); CHECK(!none.ok);
#endif
    fs::remove_all(tmp);
    std::printf("%d/%d checks passed\n", total - fails, total);
    return fails ? 1 : 0;
}
