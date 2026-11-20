#include "archive.h"
#include "miniz.h"
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

static bool isAlreadyCompressed(const std::string& n) {
    static const char* ex[] = {".png", ".jpg", ".jpeg", ".ogg", ".mp3", ".mp4", ".webp", ".zip", ".jar", ".gz", nullptr};
    std::string l; for (char c : n) l.push_back((c >= 'A' && c <= 'Z') ? c + 32 : c);
    for (int i = 0; ex[i]; ++i) { std::string e = ex[i]; if (l.size() >= e.size() && l.compare(l.size() - e.size(), e.size(), e) == 0) return true; }
    return false;
}

namespace he {

struct ZipReader::Impl { mz_zip_archive z; FILE* f = nullptr; bool open = false; std::map<std::string, mz_uint> index; };

ZipReader::ZipReader() : d_(new Impl) { std::memset(&d_->z, 0, sizeof(d_->z)); }
ZipReader::~ZipReader() { close(); }
void ZipReader::close() {
    if (d_->open) { mz_zip_reader_end(&d_->z); d_->open = false; }
    if (d_->f) { std::fclose(d_->f); d_->f = nullptr; }
    d_->index.clear();
    std::memset(&d_->z, 0, sizeof(d_->z));
}
bool ZipReader::isOpen() const { return d_->open; }

bool ZipReader::open(const std::string& path, std::string& err) {
    close();
    // miniz opens by narrow path; on Windows go through a FILE* so UTF-8 paths work.
#ifdef _WIN32
    int wlen = MultiByteToWideChar(65001, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wp(wlen ? wlen - 1 : 0, L'\0');
    if (wlen) MultiByteToWideChar(65001, 0, path.c_str(), -1, &wp[0], wlen);
    FILE* f = _wfopen(wp.c_str(), L"rb");
#else
    FILE* f = std::fopen(path.c_str(), "rb");
#endif
    if (!f) { err = "cannot open archive"; return false; }
    if (!mz_zip_reader_init_cfile(&d_->z, f, 0, 0)) {
        err = std::string("not a valid zip archive: ") + mz_zip_get_error_string(mz_zip_get_last_error(&d_->z));
        std::fclose(f); std::memset(&d_->z, 0, sizeof(d_->z)); return false;
    }
    d_->f = f;   // miniz does not close a FILE* it was handed; close() does
    d_->open = true;
    mz_uint n = mz_zip_reader_get_num_files(&d_->z);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (mz_zip_reader_file_stat(&d_->z, i, &st)) d_->index[st.m_filename] = i;
    }
    return true;
}

std::vector<ArchiveEntry> ZipReader::entries() const {
    std::vector<ArchiveEntry> out;
    if (!d_->open) return out;
    mz_uint n = mz_zip_reader_get_num_files(const_cast<mz_zip_archive*>(&d_->z));
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(const_cast<mz_zip_archive*>(&d_->z), i, &st)) continue;
        ArchiveEntry e; e.name = st.m_filename; e.size = st.m_uncomp_size; e.isDir = mz_zip_reader_is_file_a_directory(const_cast<mz_zip_archive*>(&d_->z), i) != 0;
        out.push_back(e);
    }
    return out;
}
bool ZipReader::has(const std::string& name) const { return d_->index.count(name) != 0; }

bool ZipReader::extract(const std::string& name, std::vector<uint8_t>& out, std::string& err) {
    if (!d_->open) { err = "archive is not open"; return false; }
    auto it = d_->index.find(name);
    if (it == d_->index.end()) { err = "no such entry in the archive: " + name; return false; }
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&d_->z, it->second, &st)) { err = "cannot stat entry"; return false; }
    if (st.m_uncomp_size > 512ull * 1024 * 1024) { err = "entry too large"; return false; }
    out.resize((size_t)st.m_uncomp_size);
    if (st.m_uncomp_size && !mz_zip_reader_extract_to_mem(&d_->z, it->second, out.data(), out.size(), 0)) {
        err = std::string("extract failed: ") + mz_zip_get_error_string(mz_zip_get_last_error(&d_->z)); return false;
    }
    return true;
}

bool rewriteZip(const std::string& srcPath, const std::string& dstPath,
                const std::map<std::string, std::vector<uint8_t> >& rep, std::string& err) {
    ZipReader r;
    if (!r.open(srcPath, err)) return false;
    std::vector<ArchiveEntry> es = r.entries();
    mz_zip_archive w; std::memset(&w, 0, sizeof(w));
#ifdef _WIN32
    int wlen = MultiByteToWideChar(65001, 0, dstPath.c_str(), -1, nullptr, 0);
    std::wstring wp(wlen ? wlen - 1 : 0, L'\0');
    if (wlen) MultiByteToWideChar(65001, 0, dstPath.c_str(), -1, &wp[0], wlen);
    FILE* f = _wfopen(wp.c_str(), L"wb");
#else
    FILE* f = std::fopen(dstPath.c_str(), "wb");
#endif
    if (!f) { err = "cannot create " + dstPath; return false; }
    if (!mz_zip_writer_init_cfile(&w, f, 0)) { err = "cannot start writing the archive"; std::fclose(f); return false; }
    bool ok = true;
    for (size_t i = 0; i < es.size() && ok; ++i) {
        const ArchiveEntry& e = es[i];
        if (e.isDir) { ok = mz_zip_writer_add_mem(&w, e.name.c_str(), nullptr, 0, MZ_NO_COMPRESSION) != 0; continue; }
        std::vector<uint8_t> data; 
        auto it = rep.find(e.name);
        if (it != rep.end()) data = it->second;
        else if (!r.extract(e.name, data, err)) { ok = false; break; }
        // keep already-compressed media stored; compress everything else
        bool store = isAlreadyCompressed(e.name);
        ok = mz_zip_writer_add_mem(&w, e.name.c_str(), data.data(), data.size(), store ? MZ_NO_COMPRESSION : MZ_DEFAULT_LEVEL) != 0;
        if (!ok) err = "cannot add " + e.name;
    }
    if (ok) ok = mz_zip_writer_finalize_archive(&w) != 0;
    if (!ok && err.empty()) err = "finalizing the archive failed";
    mz_zip_writer_end(&w);
    std::fclose(f);
    return ok;
}

} // namespace he
