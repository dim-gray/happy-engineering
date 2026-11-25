#include <cstdio>
#include <fstream>
#include <filesystem>
#include <map>
#include <thread>
#include <chrono>
#include "miniz.h"
#include "core/sha256.h"
#include "core/install.h"
#include "app/toolbox.h"
namespace fs = std::filesystem;
using namespace heng;
static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::string makeZip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive z; memset(&z, 0, sizeof z);
    mz_zip_writer_init_file(&z, out.string().c_str(), 0);
    for (auto& f : files) mz_zip_writer_add_mem(&z, f.first.c_str(), f.second.data(), f.second.size(), MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&z); mz_zip_writer_end(&z);
    std::ifstream in(out, std::ios::binary); return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Fake : he::IDownloader {
    std::map<std::string, std::string> files;     // url -> bytes
    std::vector<std::string> asked;
    bool getString(const std::string& url, std::string& out, std::string& err, std::atomic<bool>*) override {
        asked.push_back(url); auto it = files.find(url); if (it == files.end()) { err = "404"; return false; } out = it->second; return true; }
    bool getFile(const std::string& url, const std::string& dest, std::string& sha, std::string& err, std::atomic<bool>*, const std::function<void(uint64_t, uint64_t)>& pr) override {
        asked.push_back(url); auto it = files.find(url); if (it == files.end()) { err = "404"; return false; }
        std::ofstream o(dest, std::ios::binary); o << it->second; sha = Sha256::of(it->second); if (pr) pr(it->second.size(), it->second.size()); return true; }
};

int main() {
    CHECK(Sha256::of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    { Sha256 h; std::string big(1000000, 'a'); for (size_t i = 0; i < big.size(); i += 777) h.update(big.data() + i, std::min<size_t>(777, big.size() - i));
      CHECK(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"); }

    std::string gh = R"({"tag_name":"6.2.4","assets":[{"name":"checksums.txt","browser_download_url":"https://github.com/x/y/a.txt","size":10},
        {"name":"radare2-6.2.4-w64.zip","browser_download_url":"https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip","size":13637072,"digest":"sha256:AA3454FF"}]})";
    ResolvedDownload r = pickGithubAsset(gh, "^radare2-[0-9.]+-w64\\.zip$");
    CHECK(r.error.empty() && r.name == "radare2-6.2.4-w64.zip" && r.size == 13637072 && r.sha256 == "aa3454ff");
    CHECK(!pickGithubAsset(gh, "^nothing$").error.empty() && !pickGithubAsset("garbage", "x").error.empty());
    ResolvedDownload a = pickAdoptiumAsset(R"([{"binaries":[{"package":{"name":"jdk.zip","link":"https://github.com/adoptium/x/jdk.zip","checksum":"ABCD","size":5}}]}])");
    CHECK(a.error.empty() && a.url.find("jdk.zip") != std::string::npos && a.sha256 == "abcd");

    std::string h, p; bool tls; int port;
    CHECK(splitUrl("https://github.com/a/b?x=1", h, p, tls, port) && h == "github.com" && p == "/a/b?x=1");
    CHECK(!splitUrl("http://github.com/a", h, p, tls, port) && !splitUrl("ftp://x", h, p, tls, port));
    CHECK(isAllowedDownloadHost("GitHub.com") && isAllowedDownloadHost("api.adoptium.net") && !isAllowedDownloadHost("evil.example") && !isAllowedDownloadHost("github.com.evil.io"));

    auto w64 = installCatalog(true, true), w32 = installCatalog(true, false), l64 = installCatalog(false, true), l32 = installCatalog(false, false);
    CHECK(findInstallFor(w64, "ghidra") && findInstallFor(w64, "ghidra")->needs.size() == 1 && findInstallFor(w64, "gcc") && findInstallFor(w64, "gcc")->package == "winlibs");
    CHECK(!findInstallFor(w32, "ghidra") && findInstallFor(w32, "gcc") && findInstallFor(w32, "gcc")->assetRegex.find("i686") != std::string::npos);
    CHECK(findInstallFor(l64, "retdec") && !findInstallFor(l64, "gcc") && !findInstallFor(l32, "ghidra"));
    CHECK(std::regex_match("winlibs-i686-posix-dwarf-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip", std::regex(findInstallFor(w32, "gcc")->assetRegex)));
    CHECK(!std::regex_match("winlibs-i686-posix-dwarf-gcc-16.2.0-llvm-23.1.3-mingw-w64ucrt-14.0.0-r3.zip", std::regex(findInstallFor(w32, "gcc")->assetRegex)));
    CHECK(std::regex_match("ghidra_12.1.4_PUBLIC_20260921.zip", std::regex(findInstallFor(w64, "ghidra")->assetRegex)));

    // ---- end to end with a fake network (pretend to be Windows so .exe names are used)
    fs::path tmp = fs::temp_directory_path() / "he_install_test"; fs::remove_all(tmp); fs::create_directories(tmp);
    std::string zipBytes = makeZip(tmp / "r2.zip", {{"radare2-6.2.4-w64/bin/radare2.exe", "MZfake"}, {"radare2-6.2.4-w64/README", "hi"}});
    std::string sha = Sha256::of(zipBytes);
    std::string json = std::string(R"({"assets":[{"name":"radare2-6.2.4-w64.zip","browser_download_url":"https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip","size":)") + std::to_string(zipBytes.size()) + R"(,"digest":"sha256:)" + sha + R"("}]})";
    Fake fk; fk.files["https://api.github.com/repos/radareorg/radare2/releases/latest"] = json;
    fk.files["https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip"] = zipBytes;
    he::ToolboxEnv env; env.windows = true; env.is64 = true; env.workDir = (tmp / "work").string(); env.toolsDir = (tmp / "tools").string();
    he::Toolbox tb; tb.configure(env, {}); tb.setDownloader(&fk); tb.rescan();
    CHECK(!tb.status("radare2").found);
    std::string why; CHECK(tb.installable("radare2", &why) && !tb.installable("clang", &why) && !why.empty());
    // not approved + user says no
    std::thread no([&] { for (int i = 0; i < 100; ++i) { he::InstallRequest rq; if (tb.pendingApproval(rq)) { CHECK(rq.package == "radare2" && rq.hasChecksum && rq.size == zipBytes.size()); tb.answerApproval(false); return; } std::this_thread::sleep_for(std::chrono::milliseconds(50)); } });
    he::InstallOutcome o = tb.install("radare2", false, nullptr); no.join();
    CHECK(!o.ok && o.message.find("did not allow") != std::string::npos && !fs::exists(tmp / "tools" / "radare2"));
    // approved
    std::thread yes([&] { for (int i = 0; i < 100; ++i) { he::InstallRequest rq; if (tb.pendingApproval(rq)) { tb.answerApproval(true); return; } std::this_thread::sleep_for(std::chrono::milliseconds(50)); } });
    o = tb.install("radare2", false, nullptr); yes.join();
    CHECK(o.ok && tb.status("radare2").found && o.message.find("SHA-256 verified") != std::string::npos);
    CHECK(!tb.installing() && tb.install("radare2", true, nullptr).message.find("already installed") != std::string::npos);
    // auto approve + checksum mismatch
    fs::remove_all(tmp / "tools"); tb.rescan(); tb.setAutoApprove(true);
    fk.files["https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip"] = zipBytes + "tamper";
    o = tb.install("radare2", false, nullptr);
    CHECK(!o.ok && o.message.find("checksum mismatch") != std::string::npos && !tb.status("radare2").found && !fs::exists(tmp / "tools" / "radare2"));
    // zip-slip
    std::string evil = makeZip(tmp / "evil.zip", {{"../evil.txt", "x"}});
    std::string ej = std::string(R"({"assets":[{"name":"radare2-6.2.4-w64.zip","browser_download_url":"https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip","size":10}]})");
    fk.files["https://api.github.com/repos/radareorg/radare2/releases/latest"] = ej; fk.files["https://github.com/radareorg/radare2/releases/download/6.2.4/radare2-6.2.4-w64.zip"] = evil;
    o = tb.install("radare2", false, nullptr);
    CHECK(!o.ok && o.message.find("unsafe") != std::string::npos && !fs::exists(tmp / "tools" / "evil.txt"));
    // foreign host is refused
    std::string fj = std::string(R"({"assets":[{"name":"radare2-6.2.4-w64.zip","browser_download_url":"https://evil.example/r2.zip","size":10}]})");
    fk.files["https://api.github.com/repos/radareorg/radare2/releases/latest"] = fj;
    o = tb.install("radare2", false, nullptr); CHECK(!o.ok && o.message.find("not an official source") != std::string::npos);
    // not installable -> hint
    o = tb.install("tcc", true, nullptr); CHECK(!o.ok && !o.message.empty());
    fs::remove_all(tmp);
    std::printf("%d/%d checks passed\n", total - fails, total);
    return fails ? 1 : 0;
}
