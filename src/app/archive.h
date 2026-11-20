// archive.h -- ZIP-family archive access via miniz (zip, jar, apk, pk3, pk4, ...).
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

namespace he {

struct ArchiveEntry { std::string name; uint64_t size = 0; bool isDir = false; };

class ZipReader {
public:
    ZipReader();
    ~ZipReader();
    bool open(const std::string& path, std::string& err);
    void close();
    bool isOpen() const;
    std::vector<ArchiveEntry> entries() const;
    bool extract(const std::string& name, std::vector<uint8_t>& out, std::string& err);
    bool has(const std::string& name) const;
private:
    struct Impl; std::unique_ptr<Impl> d_;
    ZipReader(const ZipReader&) = delete; ZipReader& operator=(const ZipReader&) = delete;
};

// Writes a copy of `srcPath` to `dstPath` where entries named in `replacements`
// carry new bytes. Entries not mentioned are copied unchanged (re-compressed).
bool rewriteZip(const std::string& srcPath, const std::string& dstPath,
                const std::map<std::string, std::vector<uint8_t> >& replacements, std::string& err);

} // namespace he
