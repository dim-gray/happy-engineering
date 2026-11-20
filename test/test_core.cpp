// test_core.cpp -- Happy Enginiring
// Exercises every platform-independent module (json, strings_extract, pe,
// provider, settings, analysis) with plain asserts. Builds with any C++11
// compiler, no Windows headers involved -- this is what we can actually
// run in a Linux dev/CI environment even though the shipped app is Win32.
//
// Build:  g++ -std=c++11 -I../src -o test_core test_core.cpp && ./test_core
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "core/json.h"
#include "core/strings_extract.h"
#include "core/pe.h"
#include "core/provider.h"
#include "core/settings.h"
#include "core/analysis.h"
#include "core/target.h"

using namespace heng;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    else { std::printf("ok   %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static void test_json() {
    // round trip
    JsonValue obj = JsonValue::makeObject();
    obj.set("model", JsonValue("claude-sonnet-5-5"));
    obj.set("max_tokens", JsonValue(2000));
    JsonValue msgs = JsonValue::makeArray();
    JsonValue m = JsonValue::makeObject();
    m.set("role", JsonValue("user"));
    m.set("content", JsonValue("Buna \"ziua\"\ncu linie noua"));
    msgs.push(m);
    obj.set("messages", msgs);

    std::string dumped = obj.dump();
    bool ok = false;
    JsonValue parsed = parseJson(dumped, &ok);
    CHECK(ok);
    CHECK(parsed.getString("model") == "claude-sonnet-5-5");
    const JsonValue* ms = parsed.find("messages");
    CHECK(ms && ms->isArray() && ms->arr->size() == 1);
    CHECK((*ms->arr)[0].find("content")->str == "Buna \"ziua\"\ncu linie noua");

    // malformed input should fail cleanly, not crash
    bool ok2 = true;
    parseJson("{not json", &ok2);
    CHECK(!ok2);

    // unicode escape decodes (naive UTF-8, ASCII range)
    bool ok3 = false;
    JsonValue u = parseJson("{\"x\":\"A\\u0042C\"}", &ok3);
    CHECK(ok3);
    CHECK(u.getString("x") == "ABC");
}

static void test_strings_extract() {
    std::vector<uint8_t> buf = {'h','i',0,0,'H','e','l','l','o',0,1,2,'W','o','r','l','d','!','!',0};
    auto a = extractAsciiStrings(buf.data(), buf.size(), 4);
    bool foundHello = false, foundWorld = false;
    for (auto& s : a) { if (s == "Hello") foundHello = true; if (s == "World!!") foundWorld = true; }
    CHECK(foundHello);
    CHECK(foundWorld);

    // UTF-16LE "Hi" => 'H',0,'i',0
    std::vector<uint8_t> w = {'H',0,'i',0,'!',0,'!',0,'2',0,0,0};
    auto u = extractUtf16Strings(w.data(), w.size(), 3);
    bool foundHi = false;
    for (auto& s : u) if (s == "Hi!!2") foundHi = true;
    CHECK(foundHi);
}

// Build a minimal, valid-enough 32-bit PE in memory: DOS header + PE sig +
// COFF header + optional header (PE32) + one section + a tiny import
// descriptor table pointing at "KERNEL32.DLL"/"ExitProcess". This is
// construction only, byte-for-byte, so no OS/linker involved.
static std::vector<uint8_t> buildFakePe32() {
    std::vector<uint8_t> f;
    auto put16 = [&](uint16_t v){ f.push_back(v & 0xFF); f.push_back((v>>8)&0xFF); };
    auto put32 = [&](uint32_t v){ for (int i=0;i<4;++i) f.push_back((v>>(8*i))&0xFF); };
    auto putStr = [&](const char* s, size_t pad){ size_t n=std::strlen(s); for(size_t i=0;i<pad;++i) f.push_back(i<n?(uint8_t)s[i]:0); };

    // --- DOS header (64 bytes) ---
    f.resize(0x40, 0);
    f[0]='M'; f[1]='Z';
    uint32_t peOffset = 0x80; // we'll place PE header at 0x80
    f[0x3C] = peOffset & 0xFF; f[0x3D]=(peOffset>>8)&0xFF; f[0x3E]=(peOffset>>16)&0xFF; f[0x3F]=(peOffset>>24)&0xFF;
    f.resize(peOffset, 0);

    size_t sectionHeaderOff, importDirOff, dllNameOff, thunkOff, hintNameOff;
    size_t rawDataStart;

    // PE signature + COFF header
    f.push_back('P'); f.push_back('E'); f.push_back(0); f.push_back(0);
    put16(0x014c);      // machine = x86
    put16(1);           // number of sections
    put32(0);            // timestamp
    put32(0);            // ptr to symtab
    put32(0);            // num symbols
    uint16_t optHeaderSize = 96; // PE32 standard+windows fields with 16 dir entries = 96+... we'll compute precisely below
    size_t optHeaderSizePos = f.size();
    put16(0); // placeholder, fixed after we know real size
    put16(0x0102); // characteristics: executable, 32-bit machine

    size_t optHeaderStart = f.size();
    // Optional header (PE32, magic 0x10b)
    put16(0x10b); // magic PE32
    f.push_back(0); f.push_back(0); // linker ver
    put32(0x200);  // size of code
    put32(0);      // size of init data
    put32(0);      // size of uninit data
    put32(0x1000); // entry point RVA
    put32(0x1000); // base of code
    put32(0x2000); // base of data (PE32 only)
    put32(0x400000); // image base
    put32(0x1000); // section alignment
    put32(0x200);  // file alignment
    put16(6); put16(0); // OS ver
    put16(0); put16(0); // image ver
    put16(6); put16(0); // subsystem ver
    put32(0); // win32 version
    put32(0x3000); // size of image
    put32(0x200);  // size of headers
    put32(0); // checksum
    put16(3); // subsystem = console
    put16(0); // dll characteristics
    put32(0x100000); put32(0x1000); // stack reserve/commit
    put32(0x100000); put32(0x1000); // heap reserve/commit
    put32(0); // loader flags
    put32(16); // number of RVA and sizes
    for (int i=0;i<16;++i) {
        if (i==1) { put32(0); put32(0); } // import dir filled below after we know rva
        else { put32(0); put32(0); }
    }
    size_t optHeaderEnd = f.size();
    uint16_t actualOptSize = static_cast<uint16_t>(optHeaderEnd - optHeaderStart);
    f[optHeaderSizePos] = actualOptSize & 0xFF; f[optHeaderSizePos+1] = (actualOptSize>>8)&0xFF;

    // Section header: ".text" covering RVA 0x1000, raw offset we set after layout
    sectionHeaderOff = f.size();
    putStr(".text", 8);
    put32(0x1000); // virtual size
    put32(0x1000); // virtual address
    put32(0x200);  // size of raw data (we'll just allocate 0x200)
    size_t rawPtrFieldPos = f.size();
    put32(0); // raw ptr, fixed below
    put32(0); put32(0); // reloc ptr, linenum ptr
    put16(0); put16(0); // reloc/linenum counts
    put32(0x60000020); // characteristics: code, exec, read

    // pad headers to file alignment 0x200
    while (f.size() < 0x200) f.push_back(0);
    rawDataStart = f.size();
    f[rawPtrFieldPos] = rawDataStart & 0xFF; f[rawPtrFieldPos+1]=(rawDataStart>>8)&0xFF;
    f[rawPtrFieldPos+2] = (rawDataStart>>16)&0xFF; f[rawPtrFieldPos+3]=(rawDataStart>>24)&0xFF;

    // Section raw content (RVA 0x1000..0x1200): layout import descriptor +
    // thunk + hint/name + dll name, all inside this one section for
    // simplicity (rva->offset mapping only needs one section that covers it).
    importDirOff = f.size();                 // RVA 0x1000
    size_t importDirRva = 0x1000;
    thunkOff = importDirOff + 20;             // one descriptor (20 bytes) then thunk array
    size_t thunkRva = importDirRva + 20;
    hintNameOff = thunkOff + 8;               // thunk array: 1 entry (4 bytes) + null terminator (4 bytes)
    size_t hintNameRva = thunkRva + 8;
    dllNameOff = hintNameOff + 2 + 12;        // hint(2) + "ExitProcess\0" (12)
    size_t dllNameRva = hintNameRva + 2 + 12;

    // import descriptor: OriginalFirstThunk, TimeStamp, ForwarderChain, Name, FirstThunk
    put32(static_cast<uint32_t>(thunkRva));
    put32(0);
    put32(0);
    put32(static_cast<uint32_t>(dllNameRva));
    put32(static_cast<uint32_t>(thunkRva)); // reuse same thunk as IAT for simplicity
    // thunk array: RVA to hint/name struct, then 0 terminator
    put32(static_cast<uint32_t>(hintNameRva));
    put32(0);
    // hint/name: hint(2) + name + nul
    put16(0);
    putStr("ExitProcess", 12);
    // dll name
    putStr("KERNEL32.DLL", 13);

    while (f.size() < rawDataStart + 0x200) f.push_back(0);

    // Now go back and fix the import directory entry (index 1) in the
    // optional header's data directories.
    size_t dirArrayStart = optHeaderStart + (optHeaderEnd - optHeaderStart) - 16*8;
    size_t importEntryPos = dirArrayStart + 1*8;
    f[importEntryPos+0] = importDirRva & 0xFF;
    f[importEntryPos+1] = (importDirRva>>8)&0xFF;
    f[importEntryPos+2] = (importDirRva>>16)&0xFF;
    f[importEntryPos+3] = (importDirRva>>24)&0xFF;
    uint32_t importDirSize = 20;
    f[importEntryPos+4] = importDirSize & 0xFF;
    f[importEntryPos+5] = (importDirSize>>8)&0xFF;
    f[importEntryPos+6] = 0; f[importEntryPos+7] = 0;

    (void)sectionHeaderOff;
    return f;
}

static void test_pe_parser() {
    auto bytes = buildFakePe32();
    PeInfo info = parsePe(bytes.data(), bytes.size());
    CHECK(info.valid);
    CHECK(!info.is64);
    CHECK(info.machineName == std::string("x86 (32-bit)"));
    CHECK(!info.isDll);
    CHECK(info.numberOfSections == 1);
    CHECK(info.sections.size() == 1);
    CHECK(info.sections[0].name == ".text");
    CHECK(info.imports.size() == 1);
    if (!info.imports.empty()) {
        CHECK(info.imports[0].dll == "KERNEL32.DLL");
        bool hasExitProcess = false;
        for (auto& fn : info.imports[0].functions) if (fn == "ExitProcess") hasExitProcess = true;
        CHECK(hasExitProcess);
    }

    // non-PE input should fail gracefully, not throw/crash
    std::vector<uint8_t> notPe = {'h','e','l','l','o'};
    PeInfo bad = parsePe(notPe.data(), notPe.size());
    CHECK(!bad.valid);
}

static void test_provider_requests() {
    ApiRequest r1 = buildRequest(Provider::OpenAI, "gpt-5.1-mini", "sk-test", "sys", "user prompt");
    CHECK(r1.host == "api.openai.com");
    CHECK(r1.path == "/v1/chat/completions");
    bool hasAuth = false;
    for (auto& h : r1.headers) if (h.first == "Authorization" && h.second == "Bearer sk-test") hasAuth = true;
    CHECK(hasAuth);
    bool jok=false; JsonValue b1 = parseJson(r1.body, &jok);
    CHECK(jok);
    CHECK(b1.getString("model") == "gpt-5.1-mini");

    ApiRequest r2 = buildRequest(Provider::Anthropic, "claude-sonnet-5-5", "ak-test", "sys-x", "hello");
    CHECK(r2.host == "api.anthropic.com");
    bool hasXKey=false;
    for (auto& h : r2.headers) if (h.first == "x-api-key" && h.second == "ak-test") hasXKey = true;
    CHECK(hasXKey);
    bool jok2=false; JsonValue b2 = parseJson(r2.body, &jok2);
    CHECK(jok2);
    CHECK(b2.getString("system") == "sys-x");

    ApiRequest r3 = buildRequest(Provider::OpenRouter, "meta-llama/llama-3.3-70b-instruct:free", "or-test", "s", "u");
    CHECK(r3.host == "openrouter.ai");

    // Response parsing
    ApiResult ok1 = parseResponse(Provider::OpenAI, 200,
        "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"raspuns ok\"}}]}");
    CHECK(ok1.ok);
    CHECK(ok1.text == "raspuns ok");

    ApiResult ok2r = parseResponse(Provider::Anthropic, 200,
        "{\"content\":[{\"type\":\"text\",\"text\":\"salut\"}]}");
    CHECK(ok2r.ok);
    CHECK(ok2r.text == "salut");

    ApiResult bad = parseResponse(Provider::OpenAI, 401,
        "{\"error\":{\"message\":\"Invalid API key\"}}");
    CHECK(!bad.ok);
    CHECK(bad.error.find("Invalid API key") != std::string::npos);
}

static void test_settings_roundtrip() {
    Settings s;
    s.provider = Provider::OpenRouter;
    s.tier = Tier::Pro;
    s.model = "anthropic/claude-sonnet-5-5";
    s.apiKeyOpenAI = "sk-abc123";
    s.apiKeyOpenRouter = "or-secret-key-!@#";
    s.apiKeyAnthropic = "";
    s.lastTargetPath = "C:\\temp\\sample.exe";

    std::string text = serializeSettings(s);
    // the key must not appear as plaintext in the serialized form
    CHECK(text.find("or-secret-key") == std::string::npos);

    Settings s2 = parseSettings(text);
    CHECK(s2.provider == Provider::OpenRouter);
    CHECK(s2.tier == Tier::Pro);
    CHECK(s2.model == "anthropic/claude-sonnet-5-5");
    CHECK(s2.apiKeyOpenAI == "sk-abc123");
    CHECK(s2.apiKeyOpenRouter == "or-secret-key-!@#");
    CHECK(s2.lastTargetPath == "C:\\temp\\sample.exe");

    // file round trip
    const char* path = "/tmp/happy_enginiring_test_settings.cfg";
    CHECK(saveSettingsToFile(s, path));
    Settings s3 = loadSettingsFromFile(path);
    CHECK(s3.apiKeyOpenRouter == "or-secret-key-!@#");
    std::remove(path);

    // missing file => defaults, not a crash
    Settings s4 = loadSettingsFromFile("/tmp/this_file_does_not_exist_hopefully.cfg");
    CHECK(s4.apiKeyAnthropic.empty());
}

static void test_analysis_context() {
    auto bytes = buildFakePe32();
    std::string ctx = buildFileContext("C:\\sample.exe", bytes);
    CHECK(ctx.find("KERNEL32.DLL") != std::string::npos);
    CHECK(ctx.find("ExitProcess") != std::string::npos);
    CHECK(ctx.find("x86 (32-bit)") != std::string::npos);

    std::string html = "<html><head><link href=\"style.css\"></head>"
                        "<body><script src=\"app.js\"></script></body></html>";
    std::string hctx = buildHtmlContext("C:\\page.html", html);
    CHECK(hctx.find("app.js") != std::string::npos);
    CHECK(hctx.find("style.css") != std::string::npos);

    std::vector<ModuleSummary> mods = { {"sample.exe","C:\\sample.exe"}, {"kernel32.dll","C:\\Windows\\System32\\kernel32.dll"} };
    std::string pctx = buildProcessContext(1234, "sample.exe", "C:\\sample.exe", mods, ctx);
    CHECK(pctx.find("PID: 1234") != std::string::npos);
    CHECK(pctx.find("kernel32.dll") != std::string::npos);

    std::string prompt = buildUserPrompt("explica entry point-ul", ctx);
    CHECK(prompt.find("explica entry point-ul") != std::string::npos);
    CHECK(prompt.find("KERNEL32.DLL") != std::string::npos);

    CHECK(std::strlen(defaultSystemPrompt()) > 20);
}

static void test_system_prompt_editing() {
    // blank / whitespace-only custom prompt falls back to the default
    CHECK(effectiveSystemPrompt("") == defaultSystemPrompt());
    CHECK(effectiveSystemPrompt("   \n\t ") == defaultSystemPrompt());

    // a real custom prompt is used as-is (trimmed)
    CHECK(effectiveSystemPrompt("  you are a funny robot  ") == "you are a funny robot");

    // settings round-trip preserves a multi-line custom prompt exactly
    Settings s;
    s.systemPrompt = "Linia unu.\nLinia doi cu \"ghilimele\" si \\ backslash.\nLinia trei.";
    std::string text = serializeSettings(s);
    CHECK(text.find("system_prompt=") != std::string::npos);
    // the raw value must not contain a literal newline in the file (one key=value per line)
    size_t lineStart = text.find("system_prompt=");
    size_t lineEnd = text.find('\n', lineStart);
    CHECK(text.substr(lineStart, lineEnd - lineStart).find("Linia doi") != std::string::npos);

    Settings s2 = parseSettings(text);
    CHECK(s2.systemPrompt == s.systemPrompt);
}

int main() {
    test_json();
    test_strings_extract();
    test_pe_parser();
    test_provider_requests();
    test_settings_roundtrip();
    test_analysis_context();
    test_system_prompt_editing();

    if (failures) {
        std::fprintf(stderr, "\n%d CHECK(s) FAILED\n", failures);
        return 1;
    }
    std::printf("\nAll checks passed.\n");
    return 0;
}
