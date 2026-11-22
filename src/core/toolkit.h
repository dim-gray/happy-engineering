// toolkit.h -- Happy Engineering
// Catalog of external reverse-engineering tools (decompilers, disassemblers) and
// build tools (compilers, assemblers), plus portable helpers to locate them and
// to build their command lines. Nothing here starts a process; see app/toolbox.
#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace heng {

enum class ToolKind { Decompiler, Disassembler, Compiler, Assembler };

inline const char* toolKindName(ToolKind k) {
    switch (k) {
        case ToolKind::Decompiler: return "Decompiler";
        case ToolKind::Disassembler: return "Disassembler";
        case ToolKind::Compiler: return "Compiler";
        case ToolKind::Assembler: return "Assembler";
    }
    return "Tool";
}

struct ToolDef {
    std::string id, name, desc, url, installWin, installLinux, notes;
    ToolKind kind = ToolKind::Decompiler;
    std::vector<std::string> exeNames;   // names searched on PATH (".exe/.bat/.cmd" added on Windows)
    std::vector<std::string> relPaths;   // inside an unpacked install dir, e.g. "support/analyzeHeadless"
    std::string dirPrefix;               // folder-name prefix of an unpacked install ("ghidra")
    std::string versionArg;              // e.g. "--version"; empty = do not probe
    bool needs64 = false;                // the tool itself cannot run on 32-bit systems
    int fixedBits = 0;                   // mingw cross compilers: 32 or 64, 0 = chosen with -m32/-m64
    bool decompiles() const { return kind == ToolKind::Decompiler || kind == ToolKind::Disassembler; }
    bool builds() const { return kind == ToolKind::Compiler || kind == ToolKind::Assembler; }
};

inline const std::vector<ToolDef>& toolCatalog() {
    static const std::vector<ToolDef> cat = [] {
        std::vector<ToolDef> v;
        auto add = [&](ToolDef d) { v.push_back(d); };
        { ToolDef d; d.id = "ghidra"; d.name = "Ghidra"; d.kind = ToolKind::Decompiler;
          d.desc = "NSA's reverse-engineering suite. Happy runs it headless and exports C-like code for every function.";
          d.url = "https://github.com/NationalSecurityAgency/ghidra/releases";
          d.installWin = "Download the zip from the releases page, unzip it anywhere (needs a 64-bit JDK 17+, e.g. Temurin), then point Happy at the folder.";
          d.installLinux = "Download the zip, unzip it, install a JDK 17+ (sudo apt install openjdk-17-jdk), then point Happy at the folder.";
          d.exeNames = {"analyzeHeadless"}; d.relPaths = {"support/analyzeHeadless", "analyzeHeadless"}; d.dirPrefix = "ghidra";
          d.needs64 = true; d.notes = "Needs 64-bit Windows/Linux and a 64-bit JDK. Big programs can take several minutes.";
          add(d); }
        { ToolDef d; d.id = "radare2"; d.name = "radare2"; d.kind = ToolKind::Decompiler;
          d.desc = "Scriptable disassembler with a built-in pseudo-C view (pdc). Lightweight, 32- and 64-bit builds exist.";
          d.url = "https://github.com/radareorg/radare2/releases";
          d.installWin = "winget install radare2   (or download the installer / zip from the releases page)";
          d.installLinux = "sudo apt install radare2";
          d.exeNames = {"r2", "radare2"}; d.relPaths = {"bin/radare2", "bin/r2", "radare2", "r2"}; d.dirPrefix = "radare2"; d.versionArg = "-v";
          add(d); }
        { ToolDef d; d.id = "rizin"; d.name = "Rizin"; d.kind = ToolKind::Disassembler;
          d.desc = "Fork of radare2. Exports annotated disassembly of every function.";
          d.url = "https://github.com/rizinorg/rizin/releases";
          d.installWin = "Download the zip from the releases page and unzip it.";
          d.installLinux = "sudo apt install rizin";
          d.exeNames = {"rizin"}; d.relPaths = {"bin/rizin", "rizin"}; d.dirPrefix = "rizin"; d.versionArg = "-v";
          add(d); }
        { ToolDef d; d.id = "retdec"; d.name = "RetDec"; d.kind = ToolKind::Decompiler;
          d.desc = "Avast's retargetable decompiler: turns executables into C.";
          d.url = "https://github.com/avast/retdec/releases";
          d.installWin = "Download the zip from the releases page and unzip it.";
          d.installLinux = "Download the tar.xz from the releases page and unpack it.";
          d.exeNames = {"retdec-decompiler"}; d.relPaths = {"bin/retdec-decompiler", "retdec-decompiler"}; d.dirPrefix = "retdec";
          d.needs64 = true;
          add(d); }
        { ToolDef d; d.id = "ilspy"; d.name = "ILSpy (ilspycmd)"; d.kind = ToolKind::Decompiler;
          d.desc = "Decompiles .NET / Unity (Mono) assemblies back to readable C#.";
          d.url = "https://github.com/icsharpcode/ILSpy/releases";
          d.installWin = "dotnet tool install -g ilspycmd";
          d.installLinux = "dotnet tool install -g ilspycmd";
          d.exeNames = {"ilspycmd"}; d.versionArg = "--version";
          d.notes = "Only for managed code (.dll/.exe built with .NET, Unity Assembly-CSharp.dll).";
          add(d); }
        { ToolDef d; d.id = "objdump"; d.name = "objdump"; d.kind = ToolKind::Disassembler;
          d.desc = "GNU disassembler (part of binutils / MinGW). Always handy, tiny, runs on 32-bit systems.";
          d.url = "https://www.mingw-w64.org/downloads/";
          d.installWin = "Comes with MinGW (also with Dev-C++'s MinGW folder).";
          d.installLinux = "sudo apt install binutils";
          d.exeNames = {"objdump"}; d.versionArg = "--version";
          add(d); }

        { ToolDef d; d.id = "gcc"; d.name = "gcc"; d.kind = ToolKind::Compiler;
          d.desc = "GNU C compiler."; d.url = "https://www.mingw-w64.org/downloads/";
          d.installWin = "Install MinGW-w64 (or MSYS2) and add its bin folder to PATH, or point Happy at gcc.exe.";
          d.installLinux = "sudo apt install gcc"; d.exeNames = {"gcc"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "g++"; d.name = "g++"; d.kind = ToolKind::Compiler;
          d.desc = "GNU C++ compiler."; d.url = "https://www.mingw-w64.org/downloads/";
          d.installWin = "Install MinGW-w64 (or MSYS2); Dev-C++ already ships one.";
          d.installLinux = "sudo apt install g++"; d.exeNames = {"g++"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "clang"; d.name = "clang"; d.kind = ToolKind::Compiler;
          d.desc = "LLVM C compiler."; d.url = "https://releases.llvm.org/";
          d.installWin = "winget install LLVM.LLVM"; d.installLinux = "sudo apt install clang";
          d.exeNames = {"clang"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "clang++"; d.name = "clang++"; d.kind = ToolKind::Compiler;
          d.desc = "LLVM C++ compiler."; d.url = "https://releases.llvm.org/";
          d.installWin = "winget install LLVM.LLVM"; d.installLinux = "sudo apt install clang";
          d.exeNames = {"clang++"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "mingw64"; d.name = "MinGW-w64 (64-bit .exe)"; d.kind = ToolKind::Compiler; d.fixedBits = 64;
          d.desc = "Builds 64-bit Windows .exe/.dll (cross-compile from Linux, or native on Windows).";
          d.url = "https://www.mingw-w64.org/downloads/";
          d.installWin = "Install MSYS2 (ucrt64/mingw64) or MinGW-w64 x86_64.";
          d.installLinux = "sudo apt install mingw-w64";
          d.exeNames = {"x86_64-w64-mingw32-gcc"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "mingw32"; d.name = "MinGW-w64 (32-bit .exe)"; d.kind = ToolKind::Compiler; d.fixedBits = 32;
          d.desc = "Builds 32-bit Windows .exe/.dll - the right choice for old 32-bit PCs.";
          d.url = "https://www.mingw-w64.org/downloads/";
          d.installWin = "Install MinGW-w64 i686 / MSYS2 mingw32, or use the MinGW bundled with Dev-C++.";
          d.installLinux = "sudo apt install mingw-w64";
          d.exeNames = {"i686-w64-mingw32-gcc"}; d.versionArg = "--version"; add(d); }
        { ToolDef d; d.id = "tcc"; d.name = "Tiny C Compiler"; d.kind = ToolKind::Compiler;
          d.desc = "Very small and fast C compiler. Good for quick experiments."; d.url = "https://bellard.org/tcc/";
          d.installWin = "Download the zip from bellard.org/tcc and unzip it."; d.installLinux = "sudo apt install tcc";
          d.exeNames = {"tcc"}; d.relPaths = {"tcc"}; d.dirPrefix = "tcc"; d.versionArg = "-v"; add(d); }
        { ToolDef d; d.id = "msvc"; d.name = "MSVC (cl)"; d.kind = ToolKind::Compiler;
          d.desc = "Microsoft C/C++ compiler. Only found when started from a Developer Command Prompt."; d.url = "https://visualstudio.microsoft.com/downloads/";
          d.installWin = "Install Visual Studio Build Tools, run Happy from the 'Developer Command Prompt'.";
          d.installLinux = "Windows only."; d.exeNames = {"cl"}; add(d); }
        { ToolDef d; d.id = "nasm"; d.name = "NASM"; d.kind = ToolKind::Assembler;
          d.desc = "Netwide Assembler: assemble .asm patches/stubs to raw bytes or object files."; d.url = "https://www.nasm.us/";
          d.installWin = "winget install nasm"; d.installLinux = "sudo apt install nasm";
          d.exeNames = {"nasm"}; d.relPaths = {"nasm"}; d.dirPrefix = "nasm"; d.versionArg = "-v"; add(d); }
        return v;
    }();
    return cat;
}

inline const ToolDef* findToolDef(const std::string& id) {
    for (const ToolDef& d : toolCatalog()) if (d.id == id) return &d;
    return nullptr;
}

// ---- locating -------------------------------------------------------------
namespace toolkit_detail {
inline std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline bool isFile(const std::filesystem::path& p) { std::error_code ec; return std::filesystem::is_regular_file(p, ec); }
inline bool isDir(const std::filesystem::path& p) { std::error_code ec; return std::filesystem::is_directory(p, ec); }
inline std::string tryFile(const std::filesystem::path& base, bool win) {
    if (win) {
        static const char* ex[] = {".exe", ".bat", ".cmd", ""};
        for (const char* e : ex) { std::filesystem::path p = base; p += e; if (isFile(p)) return p.string(); }
    } else if (isFile(base)) return base.string();
    return std::string();
}
} // namespace toolkit_detail

inline std::vector<std::string> splitPathList(const std::string& env, bool win) {
    std::vector<std::string> out; std::string cur; char sep = win ? ';' : ':';
    for (char c : env) { if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur.push_back(c); }
    if (!cur.empty()) out.push_back(cur);
    for (std::string& s : out) if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return out;
}

// Resolve a user supplied path: a file, or an install folder containing one of relPaths / exeNames.
inline std::string resolveToolPath(const ToolDef& d, const std::string& userPath, bool win) {
    using namespace toolkit_detail;
    if (userPath.empty()) return std::string();
    std::filesystem::path up(userPath);
    if (isFile(up)) return userPath;
    if (!isDir(up)) return std::string();
    for (const std::string& r : d.relPaths) { std::string f = tryFile(up / r, win); if (!f.empty()) return f; }
    for (const std::string& n : d.exeNames) { std::string f = tryFile(up / "bin" / n, win); if (!f.empty()) return f; f = tryFile(up / n, win); if (!f.empty()) return f; }
    // one level down (e.g. unzip created ghidra_11.x_PUBLIC/ inside the chosen folder)
    std::error_code ec;
    for (std::filesystem::directory_iterator it(up, ec), end; !ec && it != end; it.increment(ec)) {
        if (!isDir(it->path())) continue;
        for (const std::string& r : d.relPaths) { std::string f = tryFile(it->path() / r, win); if (!f.empty()) return f; }
    }
    return std::string();
}

// Search PATH, then unpacked installs found in `roots` (folders whose name starts with dirPrefix).
inline std::string locateTool(const ToolDef& d, const std::string& userPath,
                              const std::vector<std::string>& pathDirs, const std::vector<std::string>& roots, bool win) {
    using namespace toolkit_detail;
    std::string u = resolveToolPath(d, userPath, win);
    if (!u.empty()) return u;
    for (const std::string& dir : pathDirs)
        for (const std::string& n : d.exeNames) { std::string f = tryFile(std::filesystem::path(dir) / n, win); if (!f.empty()) return f; }
    if (!d.dirPrefix.empty()) {
        std::string pre = lower(d.dirPrefix);
        for (const std::string& root : roots) {
            std::error_code ec;
            if (!isDir(root)) continue;
            for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
                if (!isDir(it->path())) continue;
                std::string nm = lower(it->path().filename().string());
                if (nm.compare(0, pre.size(), pre) != 0) continue;
                std::string f = resolveToolPath(d, it->path().string(), win);
                if (!f.empty()) return f;
            }
        }
    }
    return std::string();
}

// ---- command lines --------------------------------------------------------
inline std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out; std::string cur; bool inq = false, any = false; char q = 0;
    for (char c : s) {
        if (inq) { if (c == q) inq = false; else cur.push_back(c); }
        else if (c == '"' || c == '\'') { inq = true; q = c; any = true; }
        else if (std::isspace((unsigned char)c)) { if (any || !cur.empty()) out.push_back(cur); cur.clear(); any = false; }
        else cur.push_back(c);
    }
    if (any || !cur.empty()) out.push_back(cur);
    return out;
}

struct CmdSpec {
    std::vector<std::string> argv;
    std::string outFile;        // the tool writes its result here; empty = capture stdout instead
    std::string outExt = "txt"; // extension for captured stdout / description of the result
    std::string mkdir;          // folder that must exist before the run (e.g. ghidra project dir)
    std::string error;
};

struct DecompileOpts { std::string function; int maxFunctions = 1500; };

// Name of the Java script Happy writes next to its work files for Ghidra.
inline const char* ghidraScriptName() { return "HappyDecompile.java"; }
inline const char* ghidraScriptSource() {
    return R"JAVA(// Generated by Happy Engineering. Args: <outFile> <maxFunctions> <functionFilter or *>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import java.io.File;
import java.io.PrintWriter;

public class HappyDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length < 1) { println("HappyDecompile: missing output path"); return; }
        int max = a.length > 1 ? Integer.parseInt(a[1]) : 1500;
        String filter = (a.length > 2 && !a[2].equals("*")) ? a[2].toLowerCase() : "";
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int done = 0;
        try (PrintWriter w = new PrintWriter(new File(a[0]), "UTF-8")) {
            w.println("// Decompiled by Ghidra via Happy Engineering: " + currentProgram.getName());
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && done < max && !monitor.isCancelled()) {
                Function f = it.next();
                if (f.isThunk()) continue;
                if (!filter.isEmpty() && !f.getName().toLowerCase().contains(filter)) continue;
                DecompileResults r = di.decompileFunction(f, 30, monitor);
                if (r != null && r.decompileCompleted()) {
                    w.println();
                    w.println("// ---- " + f.getName() + " @ " + f.getEntryPoint() + " ----");
                    w.println(r.getDecompiledFunction().getC());
                    done++;
                }
            }
            w.println();
            w.println("// " + done + " function(s) exported");
        }
        di.dispose();
    }
}
)JAVA";
}

inline std::string baseNameNoDir(const std::string& p) {
    size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? p : p.substr(i + 1);
}

inline CmdSpec buildDecompileCmd(const ToolDef& d, const std::string& exe, const std::string& input,
                                 const std::string& outBase /* path without extension */, const std::string& workDir,
                                 const DecompileOpts& o) {
    CmdSpec c; c.argv.push_back(exe);
    std::string fn = o.function;
    for (char& ch : fn) if (!(std::isalnum((unsigned char)ch) || ch == '_' || ch == '.' || ch == '@' || ch == '?' || ch == '-')) ch = '_';   // never let a name become a tool command
    if (d.id == "ghidra") {
        c.outFile = outBase + ".c"; c.outExt = "c";
        c.mkdir = workDir + "/ghidra_proj";
        c.argv.push_back(c.mkdir); c.argv.push_back("happy_proj");
        c.argv.push_back("-import"); c.argv.push_back(input);
        c.argv.push_back("-scriptPath"); c.argv.push_back(workDir);
        c.argv.push_back("-postScript"); c.argv.push_back(ghidraScriptName());
        c.argv.push_back(c.outFile); c.argv.push_back(std::to_string(o.maxFunctions)); c.argv.push_back(fn.empty() ? "*" : fn);
        c.argv.push_back("-deleteProject");
    } else if (d.id == "radare2") {
        c.outExt = "c";
        std::string cmd = fn.empty() ? "aaa; afl; pdc @@F" : "aaa; pdc @ sym." + fn;
        c.argv.insert(c.argv.end(), {"-e", "scr.color=0", "-e", "scr.interactive=false", "-q", "-c", cmd, input});
    } else if (d.id == "rizin") {
        c.outExt = "asm";
        std::string cmd = fn.empty() ? "aaa; afl; pdf @@F" : "aaa; pdf @ sym." + fn;
        c.argv.insert(c.argv.end(), {"-e", "scr.color=0", "-q", "-c", cmd, input});
    } else if (d.id == "retdec") {
        c.outFile = outBase + ".c"; c.outExt = "c";
        c.argv.push_back(input); c.argv.push_back("-o"); c.argv.push_back(c.outFile);
    } else if (d.id == "ilspy") {
        c.outExt = "cs"; c.argv.push_back(input);
    } else if (d.id == "objdump") {
        c.outExt = "asm";
        c.argv.insert(c.argv.end(), {"-d", "-C", "-M", "intel", "--no-show-raw-insn", input});
    } else {
        c.error = "'" + d.name + "' cannot decompile";
    }
    return c;
}

struct CompileOpts {
    std::vector<std::string> sources;
    std::string output;
    std::string flags;     // free-form extra flags
    int bits = 0;          // 0 = compiler default, 32, 64
    std::string asmFormat; // nasm: win32/win64/elf32/elf64/bin; empty = derive from bits
    bool windowsHost = false;
};

inline CmdSpec buildCompileCmd(const ToolDef& d, const std::string& exe, const CompileOpts& o) {
    CmdSpec c; c.argv.push_back(exe); c.outFile = o.output; c.outExt = "bin";
    if (o.sources.empty()) { c.error = "no source files"; return c; }
    if (d.id == "nasm") {
        std::string fmt = o.asmFormat;
        if (fmt.empty()) fmt = o.windowsHost ? (o.bits == 64 ? "win64" : "win32") : (o.bits == 64 ? "elf64" : "elf32");
        c.argv.push_back("-f"); c.argv.push_back(fmt);
        for (const std::string& f : splitArgs(o.flags)) c.argv.push_back(f);
        c.argv.push_back(o.sources[0]); c.argv.push_back("-o"); c.argv.push_back(o.output);
        return c;
    }
    if (d.id == "msvc") {
        for (const std::string& s : o.sources) c.argv.push_back(s);
        c.argv.push_back("/Fe:" + o.output);
        for (const std::string& f : splitArgs(o.flags)) c.argv.push_back(f);
        return c;
    }
    if (!d.builds() || d.kind != ToolKind::Compiler) { c.error = "'" + d.name + "' is not a compiler"; return c; }
    for (const std::string& s : o.sources) c.argv.push_back(s);
    c.argv.push_back("-o"); c.argv.push_back(o.output);
    bool mflag = (d.id == "gcc" || d.id == "g++" || d.id == "clang" || d.id == "clang++");
    if (mflag && o.bits == 32) c.argv.push_back("-m32");
    if (mflag && o.bits == 64) c.argv.push_back("-m64");
    for (const std::string& f : splitArgs(o.flags)) c.argv.push_back(f);
    return c;
}

// "c", "cpp", "asm" from a file name (empty if unknown)
inline std::string sourceLang(const std::string& file) {
    size_t dot = file.find_last_of('.'); if (dot == std::string::npos) return "";
    std::string e = toolkit_detail::lower(file.substr(dot + 1));
    if (e == "c") return "c";
    if (e == "cpp" || e == "cc" || e == "cxx" || e == "c++") return "cpp";
    if (e == "asm" || e == "nasm") return "asm";
    return "";
}

// Whether a compiler id can build the given language
inline bool compilerSupports(const ToolDef& d, const std::string& lang) {
    if (lang == "asm") return d.id == "nasm";
    if (d.id == "nasm") return false;
    if (lang == "cpp") return d.id == "g++" || d.id == "clang++" || d.id == "msvc";
    if (lang == "c") return d.kind == ToolKind::Compiler;
    return false;
}

// Pull code blocks (```lang ... ```) out of an AI reply. Returns the bodies; `langs` gets the fence tags.
inline std::vector<std::string> extractCodeBlocks(const std::string& text, std::vector<std::string>* langs = nullptr) {
    std::vector<std::string> out; size_t pos = 0;
    while (true) {
        size_t a = text.find("```", pos); if (a == std::string::npos) break;
        size_t nl = text.find('\n', a); if (nl == std::string::npos) break;
        size_t b = text.find("```", nl); if (b == std::string::npos) break;
        std::string tag = text.substr(a + 3, nl - a - 3);
        while (!tag.empty() && std::isspace((unsigned char)tag.back())) tag.pop_back();
        std::string body = text.substr(nl + 1, b - nl - 1);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
        if (tag.compare(0, 5, "happy") != 0) { out.push_back(body); if (langs) langs->push_back(tag); }
        pos = b + 3;
    }
    return out;
}

} // namespace heng
