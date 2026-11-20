// test_edit.cpp -- Happy Enginiring
// Tests for the editing side of the portable core: base64, attachment
// inspection, plan parsing, patcher, memory scanner and the plan executor.
// Build:  g++ -std=c++11 -Wall -Wextra -I../src -o test_edit test_edit.cpp && ./test_edit
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/base64.h"
#include "core/attachments.h"
#include "core/actions.h"
#include "core/patcher.h"
#include "core/scanner.h"
#include "core/executor.h"
#include "core/context.h"
#include "core/provider.h"

using namespace heng;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    else { std::printf("ok   %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static std::vector<uint8_t> bytesOf(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

static void test_base64() {
    CHECK(base64Encode(bytesOf("")) == "");
    CHECK(base64Encode(bytesOf("M")) == "TQ==");
    CHECK(base64Encode(bytesOf("Ma")) == "TWE=");
    CHECK(base64Encode(bytesOf("Man")) == "TWFu");
    CHECK(base64Encode(bytesOf("Hello, World!")) == "SGVsbG8sIFdvcmxkIQ==");
}

static void test_textutil() {
    std::vector<uint8_t> u16;
    CHECK(utf8ToUtf16le("Ab", u16));
    CHECK(u16.size() == 4 && u16[0] == 'A' && u16[1] == 0 && u16[2] == 'b' && u16[3] == 0);
    // Romanian diacritic: s with comma below (U+0219) -> 0x19 0x02
    CHECK(utf8ToUtf16le("\xC8\x99", u16));
    CHECK(u16.size() == 2 && u16[0] == 0x19 && u16[1] == 0x02);
    CHECK(utf16ToUtf8(&u16[0], 2, false) == "\xC8\x99");
    CHECK(!utf8ToUtf16le("\xC8", u16));   // truncated sequence
    std::vector<uint8_t> bom = {0xEF, 0xBB, 0xBF, 'h', 'i'};
    CHECK(decodeTextBytes(bom.data(), bom.size()) == "hi");
    CHECK(humanSize(2048) == "2.0 KB");
}

static void test_attachments() {
    CHECK(classifyByExtension("logo.PNG") == AttachmentKind::ImageVision);
    CHECK(classifyByExtension("tex.dds") == AttachmentKind::ImageOther);
    CHECK(classifyByExtension("hit.wav") == AttachmentKind::Audio);
    CHECK(classifyByExtension("en.ini") == AttachmentKind::Text);
    CHECK(classifyByExtension("game.exe") == AttachmentKind::Binary);
    CHECK(imageMimeFor("a.jpeg") == "image/jpeg");
    CHECK(extensionOf("C:\\x\\y.Tar.GZ") == "gz");

    // minimal PNG header 512x256
    std::vector<uint8_t> png = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A, 0,0,0,13,'I','H','D','R',
                                0,0,2,0, 0,0,1,0, 8,6,0,0,0};
    MediaInfo mi = inspectMedia(png, "a.png");
    CHECK(mi.known && mi.format == "PNG" && mi.width == 512 && mi.height == 256);
    CHECK(describeMedia(mi) == "PNG 512x256");

    // WAV: PCM 44100 Hz, 16-bit, 2 ch, 1 second of data
    std::vector<uint8_t> wav = bytesOf("RIFF");
    auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) wav.push_back((v >> (8 * i)) & 0xFF); };
    auto le16 = [&](uint16_t v) { wav.push_back(v & 0xFF); wav.push_back(v >> 8); };
    le32(36 + 176400);
    for (char c : std::string("WAVEfmt ")) wav.push_back(c);
    le32(16); le16(1); le16(2); le32(44100); le32(176400); le16(4); le16(16);
    for (char c : std::string("data")) wav.push_back(c);
    le32(176400);
    wav.resize(wav.size() + 176400, 0);
    MediaInfo w = inspectMedia(wav, "a.wav");
    CHECK(w.known && w.format == "WAV PCM" && w.sampleRate == 44100 && w.channels == 2 && w.bits == 16);
    CHECK(w.seconds > 0.99 && w.seconds < 1.01);

    // BMP 10x20
    std::vector<uint8_t> bmp(40, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; bmp[18] = 10; bmp[22] = 20;
    MediaInfo b = inspectMedia(bmp, "a.bmp");
    CHECK(b.known && b.width == 10 && b.height == 20);

    // not media at all
    MediaInfo none = inspectMedia(bytesOf("just some text here"), "a.txt");
    CHECK(!none.known && none.format.empty());
}

static void test_plan_parsing() {
    std::string reply =
        "Am gasit textul. Iata planul:\n```happy-plan\n"
        "{\"summary\":\"Traducere meniu\",\"actions\":["
        "{\"type\":\"replace_text\",\"file\":\"data\\\\lang\\\\en.ini\",\"find\":\"New Game\",\"replace\":\"Joc Nou\",\"note\":\"meniu\"},"
        "{\"type\":\"replace_file\",\"file\":\"img/logo.png\",\"with_attachment\":\"logo_nou.png\"},"
        "{\"type\":\"patch_bytes\",\"file\":\"game.exe\",\"offset\":\"0x10\",\"expect\":\"74 05\",\"write\":\"EB 05\"},"
        "{\"type\":\"mem_write\",\"label\":\"gold\",\"value\":99999},"
        "{\"type\":\"replace_text\",\"file\":\"../../windows/system32/x.dll\",\"find\":\"a\",\"replace\":\"b\"},"
        "{\"type\":\"explode\"}"
        "],\"need_files\":[\"data/extra.txt\",\"..\\\\bad\"]}\n```\nSper ca ajuta.";
    Plan p = extractPlan(reply);
    CHECK(p.found && p.error.empty());
    CHECK(p.summary == "Traducere meniu");
    CHECK(p.actions.size() == 6);
    CHECK(p.actions[0].type == ActionType::ReplaceText && p.actions[0].error.empty());
    CHECK(p.actions[0].file == "data/lang/en.ini");             // backslashes normalized
    CHECK(p.actions[1].type == ActionType::ReplaceFile && p.actions[1].attachment == "logo_nou.png");
    CHECK(p.actions[2].type == ActionType::PatchBytes && p.actions[2].offset == 16 &&
          p.actions[2].write.size() == 2 && p.actions[2].write[0] == 0xEB);
    CHECK(p.actions[3].type == ActionType::MemWrite && p.actions[3].value == "99999");  // number accepted
    CHECK(!p.actions[4].error.empty());                         // path traversal refused
    CHECK(p.actions[5].type == ActionType::Unknown && !p.actions[5].error.empty());
    CHECK(p.needFiles.size() == 1 && p.needFiles[0] == "data/extra.txt");
    CHECK(stripPlanBlock(reply).find("happy-plan") == std::string::npos);
    CHECK(stripPlanBlock(reply).find("Sper ca ajuta.") != std::string::npos);

    CHECK(!extractPlan("fara plan aici").found);
    Plan cut = extractPlan("text\n```happy-plan\n{\"actions\":[");
    CHECK(cut.found && !cut.error.empty());                     // truncated reply is reported
    Plan bad = extractPlan("```happy-plan\n{nu e json}\n```");
    CHECK(bad.found && !bad.error.empty());

    std::string rel;
    CHECK(normalizeRelPath("a/b\\c.txt", rel) && rel == "a/b/c.txt");
    CHECK(!normalizeRelPath("/etc/passwd", rel));
    CHECK(!normalizeRelPath("C:\\Windows\\x", rel));
    CHECK(!normalizeRelPath("a/../b", rel));
    CHECK(!normalizeRelPath("file.txt:stream", rel));
    // device names and trailing dot/space aliases are refused
    CHECK(!normalizeRelPath("NUL", rel));
    CHECK(!normalizeRelPath("data/con.txt", rel));
    CHECK(!normalizeRelPath("Aux.cfg", rel));
    CHECK(!normalizeRelPath("a/COM1", rel));
    CHECK(!normalizeRelPath("lpt9.dat", rel));
    CHECK(normalizeRelPath("com10.txt", rel) && rel == "com10.txt");     // not a device
    CHECK(normalizeRelPath("console.ini", rel) && rel == "console.ini"); // not a device
    CHECK(!normalizeRelPath("readme.txt.", rel));
    CHECK(!normalizeRelPath("dir /x.txt", rel));
    CHECK(normalizeRelPath("./.hidden/x.cfg", rel) && rel == ".hidden/x.cfg");
    CHECK(isReservedWindowsName("PRN") && !isReservedWindowsName("printer"));
    uint64_t v;
    CHECK(parseUInt64("0xFF", v) && v == 255);
    CHECK(!parseUInt64("12x", v));
    std::vector<uint8_t> hx;
    CHECK(parseHexBytes("0x74, 05", hx) && hx.size() == 2 && hx[0] == 0x74 && hx[1] == 5);
    CHECK(!parseHexBytes("7", hx));
}

static void test_patcher() {
    // text file: length may change
    std::vector<uint8_t> text = bytesOf("start=New Game\nquit=New Game\n");
    PlanAction a; a.type = ActionType::ReplaceText; a.find = "New Game"; a.replace = "Joc Nou si lung"; a.file = "x.ini";
    PatchOutcome o = applyReplaceText(text, a, "x.ini");
    CHECK(o.ok && o.count == 2);
    CHECK(std::string(text.begin(), text.end()) == "start=Joc Nou si lung\nquit=Joc Nou si lung\n");

    // only the first occurrence
    std::vector<uint8_t> t2 = bytesOf("aa aa aa");
    PlanAction f; f.find = "aa"; f.replace = "b"; f.all = false;
    CHECK(applyReplaceText(t2, f, "t.txt").ok);
    CHECK(std::string(t2.begin(), t2.end()) == "b aa aa");

    // missing text
    std::vector<uint8_t> t3 = bytesOf("nothing");
    PlanAction m; m.find = "zzz"; m.replace = "y";
    CHECK(!applyReplaceText(t3, m, "t.txt").ok);

    // binary file: shorter is padded, longer is refused
    std::vector<uint8_t> bin = {0x00, 0x01, 'H', 'e', 'l', 'l', 'o', 0x00, 0x02, 0x00};
    PlanAction s; s.find = "Hello"; s.replace = "Hi";
    PatchOutcome so = applyReplaceText(bin, s, "game.exe");
    CHECK(so.ok && bin.size() == 10);
    CHECK(bin[2] == 'H' && bin[3] == 'i' && bin[4] == 0 && bin[5] == 0 && bin[6] == 0);
    PlanAction lg; lg.find = "Hi"; lg.replace = "Hello World";
    CHECK(!applyReplaceText(bin, lg, "game.exe").ok);

    // UTF-16LE text inside a binary
    std::vector<uint8_t> w = {0xFF, 0x00, 'O', 0, 'K', 0, 0, 0, 0xFF};
    PlanAction u; u.find = "OK"; u.replace = "NO"; u.encoding = "utf16le";
    CHECK(applyReplaceText(w, u, "a.bin").ok);
    CHECK(w[2] == 'N' && w[4] == 'O');

    // ascii encoding refuses non-ASCII
    std::vector<uint8_t> t4 = bytesOf("abc");
    PlanAction asc; asc.find = "abc"; asc.replace = "\xC8\x99"; asc.encoding = "ascii";
    CHECK(!applyReplaceText(t4, asc, "t.txt").ok);

    // patch_bytes with verification
    std::vector<uint8_t> code = {0x55, 0x74, 0x05, 0x90};
    PlanAction p; p.offset = 1; p.expect = {0x74, 0x05}; p.write = {0xEB, 0x05};
    CHECK(applyPatchBytes(code, p).ok && code[1] == 0xEB);
    PlanAction wrong; wrong.offset = 1; wrong.expect = {0x74, 0x05}; wrong.write = {0x90, 0x90};
    CHECK(!applyPatchBytes(code, wrong).ok);      // bytes changed: expect no longer matches
    PlanAction oob; oob.offset = 3; oob.write = {1, 2, 3};
    CHECK(!applyPatchBytes(code, oob).ok);

    // replace_file checks
    std::vector<uint8_t> pngA = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A, 0,0,0,13,'I','H','D','R', 0,0,2,0, 0,0,2,0, 8,6,0,0,0};
    std::vector<uint8_t> pngB = pngA; pngB[19] = 1;       // 256 wide instead of 512
    ReplaceCheck ok = checkReplaceFile("img/a.png", pngA, "new.png", pngB);
    CHECK(ok.ok && ok.message.find("dimensiune diferita") != std::string::npos);
    CHECK(!checkReplaceFile("img/a.png", pngA, "new.jpg", pngB).ok);
}

// A fake process memory: one region at 0x10000.
class FakeMemory : public IMemory {
public:
    FakeMemory() : data(4096, 0) {}
    std::vector<uint8_t> data;
    std::vector<MemRegion> regions() override { MemRegion r; r.base = 0x10000; r.size = data.size(); return {r}; }
    bool read(uint64_t addr, void* buf, size_t len) override {
        if (addr < 0x10000 || addr + len > 0x10000 + data.size()) return false;
        std::memcpy(buf, &data[addr - 0x10000], len); return true;
    }
    bool write(uint64_t addr, const void* buf, size_t len) override {
        if (addr < 0x10000 || addr + len > 0x10000 + data.size()) return false;
        std::memcpy(&data[addr - 0x10000], buf, len); return true;
    }
    void setInt32(size_t off, int32_t v) { std::memcpy(&data[off], &v, 4); }
    int32_t getInt32(size_t off) { int32_t v; std::memcpy(&v, &data[off], 4); return v; }
};

static void test_scanner() {
    std::vector<uint8_t> enc; std::string err;
    CHECK(parseValue(ValueType::Int32, "-2", enc, err) && enc.size() == 4 && enc[0] == 0xFE && enc[3] == 0xFF);
    CHECK(parseValue(ValueType::Int32, "0xFFFFFFFF", enc, err));
    CHECK(!parseValue(ValueType::Int32, "4294967296", enc, err));
    CHECK(!parseValue(ValueType::Int8, "300", enc, err));
    CHECK(parseValue(ValueType::Float, "1,5", enc, err));          // comma decimal accepted
    CHECK(formatValue(ValueType::Float, enc.data()) == "1.5");
    CHECK(parseValue(ValueType::Double, "2.25", enc, err) && formatValue(ValueType::Double, enc.data()) == "2.25");
    CHECK(parseValue(ValueType::Int64, "-9223372036854775808", enc, err));
    CHECK(!parseValue(ValueType::Int32, "abc", enc, err));
    ValueType vt;
    CHECK(valueTypeFromName("Float", vt) && vt == ValueType::Float);
    CHECK(!valueTypeFromName("banana", vt));

    FakeMemory mem;
    mem.setInt32(0x100, 1250);     // "gold"
    mem.setInt32(0x200, 1250);     // a decoy with the same value
    mem.setInt32(0x300, 77);
    Scanner sc(mem);
    std::vector<uint8_t> needle; parseValue(ValueType::Int32, "1250", needle, err);
    CHECK(sc.firstScan(ValueType::Int32, needle) == 2);
    CHECK(sc.hits()[0].addr == 0x10100 && sc.hits()[1].addr == 0x10200);

    mem.setInt32(0x100, 1300);     // gold changed in game, decoy did not
    CHECK(sc.nextScan(ScanMode::Changed) == 1);
    CHECK(sc.hits()[0].addr == 0x10100);
    CHECK(formatValue(ValueType::Int32, sc.hits()[0].cur) == "1300");
    CHECK(formatValue(ValueType::Int32, sc.hits()[0].prev) == "1250");

    mem.setInt32(0x100, 1400);
    CHECK(sc.nextScan(ScanMode::Increased) == 1);
    mem.setInt32(0x100, 1000);
    CHECK(sc.nextScan(ScanMode::Increased) == 0);

    // exact narrowing and writing
    Scanner sc2(mem);
    mem.setInt32(0x400, 5); mem.setInt32(0x500, 5);
    parseValue(ValueType::Int32, "5", needle, err);
    CHECK(sc2.firstScan(ValueType::Int32, needle) == 2);
    mem.setInt32(0x500, 6);
    std::vector<uint8_t> six; parseValue(ValueType::Int32, "6", six, err);
    CHECK(sc2.nextScan(ScanMode::Exact, &six) == 1 && sc2.hits()[0].addr == 0x10500);
    CHECK(writeValue(mem, sc2.hits()[0].addr, ValueType::Int32, "99999", err));
    CHECK(mem.getInt32(0x500) == 99999);
    CHECK(!writeValue(mem, 0xDEAD, ValueType::Int32, "1", err));   // invalid address

    // unaligned first scan finds a value stored at an odd offset
    mem.setInt32(0x601, 4242);
    Scanner sc3(mem);
    parseValue(ValueType::Int32, "4242", needle, err);
    CHECK(sc3.firstScan(ValueType::Int32, needle) == 0);           // natural alignment misses it
    CHECK(sc3.firstScan(ValueType::Int32, needle, 1) == 1);        // alignment 1 finds it

    // hit cap
    FakeMemory zeros;
    Scanner sc4(zeros);
    parseValue(ValueType::Int8, "0", needle, err);
    CHECK(sc4.firstScan(ValueType::Int8, needle, 0, 100) == 100 && sc4.truncated());
}

class MapStore : public IFileStore {
public:
    std::map<std::string, std::vector<uint8_t> > target, attach;
    bool readTarget(const std::string& rel, std::vector<uint8_t>& out, std::string& err) override {
        auto it = target.find(rel);
        if (it == target.end()) { err = "nu exista"; return false; }
        out = it->second; return true;
    }
    bool readAttachment(const std::string& name, std::vector<uint8_t>& out, std::string& err) override {
        auto it = attach.find(name);
        if (it == attach.end()) { err = "nu exista"; return false; }
        out = it->second; return true;
    }
};

static void test_executor() {
    MapStore store;
    store.target["lang/en.ini"] = bytesOf("menu=New Game\nexit=Quit\n");
    store.target["img/logo.png"] = std::vector<uint8_t>{0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A, 0,0,0,13,'I','H','D','R', 0,0,0,16, 0,0,0,16, 8,6,0,0,0};
    store.attach["logo2.png"] = store.target["img/logo.png"];
    store.attach["logo2.png"][19] = 32;   // different width -> warning only

    std::string reply =
        "```happy-plan\n{\"summary\":\"t\",\"actions\":["
        "{\"type\":\"replace_text\",\"file\":\"lang/en.ini\",\"find\":\"New Game\",\"replace\":\"Joc Nou\"},"
        "{\"type\":\"replace_text\",\"file\":\"lang/en.ini\",\"find\":\"Quit\",\"replace\":\"Iesire\"},"
        "{\"type\":\"replace_file\",\"file\":\"img/logo.png\",\"with_attachment\":\"logo2.png\"},"
        "{\"type\":\"replace_text\",\"file\":\"missing.txt\",\"find\":\"a\",\"replace\":\"b\"},"
        "{\"type\":\"mem_write\",\"label\":\"gold\",\"value\":\"500\"},"
        "{\"type\":\"mem_write\",\"label\":\"unknown\",\"value\":\"1\"}"
        "]}\n```";
    Plan plan = extractPlan(reply);
    CHECK(plan.actions.size() == 6);

    FakeMemory mem;
    mem.setInt32(0x100, 1250);
    std::vector<WatchEntry> watch(1);
    watch[0].label = "Gold"; watch[0].address = 0x10100; watch[0].type = ValueType::Int32;

    std::vector<bool> sel(6, true);
    sel[1] = false;                                   // user unticked "Quit -> Iesire"

    ExecutionReport dry = runPlan(plan, sel, store, watch, &mem, false);
    CHECK(dry.results[0].ok && dry.results[2].ok && !dry.results[3].ok);
    CHECK(dry.results[1].skipped);
    CHECK(dry.results[4].ok && !dry.results[5].ok);   // label resolved case-insensitively, unknown refused
    CHECK(mem.getInt32(0x100) == 1250);               // dry run must not write memory
    CHECK(dry.results[2].warning.find("dimensiune diferita") != std::string::npos);
    CHECK(dry.outputs.size() == 2);
    CHECK(std::string(dry.outputs["lang/en.ini"].begin(), dry.outputs["lang/en.ini"].end()) == "menu=Joc Nou\nexit=Quit\n");
    CHECK(store.target["lang/en.ini"] == bytesOf("menu=New Game\nexit=Quit\n"));   // original untouched

    ExecutionReport real = runPlan(plan, sel, store, watch, &mem, true);
    CHECK(real.results[4].ok && mem.getInt32(0x100) == 500);
    CHECK(real.okCount == 3 && real.failCount == 2 && real.skipCount == 1);
}

static void test_context_and_images() {
    std::vector<FileEntry> files;
    auto add = [&](const char* r, uint64_t s) { FileEntry f; f.rel = r; f.size = s; files.push_back(f); };
    add("game.exe", 5000000); add("data/lang/en.ini", 900); add("data/readme.txt", 300);
    add("data/big.txt", 90000); add("tex/a.png", 4000); add("data/config.json", 1200);
    std::vector<std::string> pick = selectSnippetFiles(files);
    CHECK(pick.size() == 3);
    CHECK(pick[0] == "data/lang/en.ini");               // "lang" files come first
    TextSnippet sn; sn.rel = "data/lang/en.ini"; sn.content = "menu=New Game";
    std::vector<TextSnippet> snips(1, sn);
    std::string ctx = buildFolderContext("C:\\Games\\X", files, false, snips);
    CHECK(ctx.find("data/lang/en.ini") != std::string::npos && ctx.find("menu=New Game") != std::string::npos);
    CHECK(ctx.find(".png x1") != std::string::npos);

    std::vector<WatchEntry> w(1); w[0].label = "gold"; w[0].address = 0x1234; w[0].lastText = "1250";
    std::string wc = buildWatchContext(w);
    CHECK(wc.find("gold") != std::string::npos && wc.find("0x1234") != std::string::npos);

    CHECK(fullSystemPrompt("").find("happy-plan") != std::string::npos);
    CHECK(fullSystemPrompt("You are a pirate.").find("You are a pirate.") == 0);

    // provider requests with images
    std::vector<ImagePart> imgs(1);
    imgs[0].label = "logo.png"; imgs[0].mime = "image/png"; imgs[0].base64 = "QUJD";
    ApiRequest oa = buildRequestWithImages(Provider::OpenAI, "gpt-5-mini", "k", "sys", "hello", imgs);
    CHECK(oa.body.find("data:image/png;base64,QUJD") != std::string::npos);
    CHECK(oa.body.find("max_completion_tokens") != std::string::npos);
    ApiRequest an = buildRequestWithImages(Provider::Anthropic, "claude-haiku-5-5", "k", "sys", "hello", imgs);
    CHECK(an.body.find("\"media_type\":\"image/png\"") != std::string::npos);
    CHECK(an.body.find("\"data\":\"QUJD\"") != std::string::npos);
    bool ok = false; parseJson(an.body, &ok); CHECK(ok);
    ApiRequest orr = buildRequestWithImages(Provider::OpenRouter, "openrouter/free", "k", "sys", "hello", imgs);
    CHECK(orr.body.find("\"max_tokens\"") != std::string::npos);
    ApiResult cut = parseResponse(Provider::OpenAI, 200,
        "{\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"abc\"}}]}");
    CHECK(cut.ok && cut.truncated);
    ApiResult cut2 = parseResponse(Provider::Anthropic, 200,
        "{\"stop_reason\":\"max_tokens\",\"content\":[{\"type\":\"text\",\"text\":\"abc\"}]}");
    CHECK(cut2.ok && cut2.truncated);
}

int main() {
    test_base64();
    test_textutil();
    test_attachments();
    test_plan_parsing();
    test_patcher();
    test_scanner();
    test_executor();
    test_context_and_images();
    if (failures) { std::fprintf(stderr, "\n%d CHECK(s) FAILED\n", failures); return 1; }
    std::printf("\nAll checks passed.\n");
    return 0;
}
