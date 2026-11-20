#include <cstdio>
#include "miniz.h"
#include "app/workspace.h"
#include "app/imgconv.h"
using namespace he;
static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static std::string slurp(const std::string& p) { std::vector<uint8_t> b; std::string e; readWholeFile(p, b, e); return std::string(b.begin(), b.end()); }

int main() {
    fs::path tmp = fs::temp_directory_path() / "he_ws_test"; fs::remove_all(tmp); fs::create_directories(tmp / "game" / "data");
    std::string root = S(tmp / "game"), err;
    std::vector<uint8_t> d1 = {'H','e','l','l','o',' ','W','o','r','l','d'};
    writeWholeFile(root + "/data/a.txt", d1, err);
    std::string zip = root + "/data.zip";
    mz_zip_add_mem_to_archive_file_in_place(zip.c_str(), "lang/en.ini", "start=Start Game\n", 17, nullptr, 0, MZ_DEFAULT_LEVEL);
    mz_zip_add_mem_to_archive_file_in_place(zip.c_str(), "tex/x.bin", "\x01\x02\x03", 3, nullptr, 0, MZ_DEFAULT_LEVEL);

    Workspace ws; heng::Target t; t.type = heng::TargetType::Folder; t.path = root; ws.setTarget(t);
    std::vector<heng::HostEntry> es;
    CHECK(ws.list("", false, 100, es, err)); CHECK(es.size() == 2);   // data/ + data.zip
    es.clear(); CHECK(ws.list("", true, 100, es, err));
    bool sawInner = false; for (auto& e : es) if (e.path == "data.zip!/lang/en.ini") sawInner = true;
    CHECK(sawInner);
    es.clear(); CHECK(ws.list("data.zip!", false, 100, es, err)); CHECK(es.size() == 2);
    std::vector<uint8_t> b; uint64_t total_;
    CHECK(ws.read("data.zip!/lang/en.ini", 6, 5, b, total_, err)); CHECK(std::string(b.begin(), b.end()) == "Start"); CHECK(total_ == 17);
    CHECK(!ws.read("nope.txt", 0, 5, b, total_, err));

    // run a plan: replace text inside the archive entry and a plain file
    heng::Plan plan = heng::extractPlan("```happy-plan\n{\"summary\":\"s\",\"actions\":["
        "{\"type\":\"replace_text\",\"file\":\"data.zip!/lang/en.ini\",\"find\":\"Start Game\",\"replace\":\"Go!\"},"
        "{\"type\":\"replace_text\",\"file\":\"data/a.txt\",\"find\":\"World\",\"replace\":\"Moon\"}]}\n```");
    CHECK(plan.actions.size() == 2);
    heng::ExecutionReport rep = heng::runPlan(plan, {true, true}, ws, {}, nullptr, false);
    CHECK(rep.okCount == 2); CHECK(rep.outputs.size() == 2);
    SaveResult sr = ws.saveOutputs(rep.outputs, S(tmp / "out"), "log");
    CHECK(sr.ok);
    CHECK(slurp(sr.dir + "/data/a.txt") == "Hello Moon");
    ZipReader z; CHECK(z.open(sr.dir + "/data.zip", err));
    CHECK(z.extract("lang/en.ini", b, err)); CHECK(std::string(b.begin(), b.end()) == "start=Go!\n");
    CHECK(z.extract("tex/x.bin", b, err)); CHECK(b.size() == 3);
    z.close();

    // install with backup, twice, then restore
    InstallResult ir = Workspace::installOutputs(sr.dir, root, true);
    CHECK(ir.ok); CHECK(ir.backedUp == 2); CHECK(slurp(root + "/data/a.txt") == "Hello Moon");
    std::vector<uint8_t> d2 = {'X'}; writeWholeFile(sr.dir + "/data/a.txt", d2, err);
    ir = Workspace::installOutputs(sr.dir, root, true);
    CHECK(ir.ok); CHECK(ir.backedUp == 0); CHECK(slurp(root + "/data/a.txt") == "X");
    CHECK(Workspace::findBackups(root).size() == 2);
    CHECK(Workspace::restoreBackups(root, err) == 2); CHECK(slurp(root + "/data/a.txt") == "Hello World");
    CHECK(Workspace::findBackups(root).empty());

    // attachments
    writeWholeFile(S(tmp / "pic.png"), {1,2,3}, err);
    CHECK(ws.addAttachment(S(tmp / "pic.png"), err)); CHECK(ws.addAttachment(S(tmp / "pic.png"), err));
    CHECK(ws.attachments().size() == 2 && ws.attachments()[1].name == "pic_2.png");

    // image conversion: 4x2 red PNG over a 8x8 original -> 8x8 png
    {
        Image a; a.w = 8; a.h = 8; a.rgba.assign(8 * 8 * 4, 255); a.hadAlpha = false;
        Image b; b.w = 4; b.h = 2; b.rgba.assign(4 * 2 * 4, 0); b.hadAlpha = false;
        for (int i = 0; i < 8; ++i) { b.rgba[i * 4] = 255; b.rgba[i * 4 + 3] = 255; }
        std::vector<uint8_t> pa, pb, outb; std::string e2, note;
        CHECK(encodeImage(a, "png", pa, e2)); CHECK(encodeImage(b, "png", pb, e2));
        ImageConverter conv;
        CHECK(conv.convert(pa, "t.png", pb, "cover", outb, note, e2));
        Image r; CHECK(decodeImage(outb, r, e2)); CHECK(r.w == 8 && r.h == 8);
        CHECK(r.rgba[0] > 240 && r.rgba[1] < 10 && r.rgba[3] == 255);   // red survives resize + crop
        CHECK(conv.convert(pa, "t.jpg", pb, "contain", outb, note, e2));   // jpg out (decodes from png bytes as 'orig' is just for size)
        CHECK(!conv.convert(pa, "t.dds", pb, "cover", outb, note, e2));
        CHECK(!conv.convert(pa, "t.png", pa, "cover", outb, note, e2) == false);
        Image c = fitImage(b, 8, 8, "contain"); CHECK(c.rgba[3] == 0);          // letterbox corner transparent
    }
    fs::remove_all(tmp);
    std::printf("%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
