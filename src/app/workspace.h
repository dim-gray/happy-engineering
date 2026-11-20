// workspace.h -- the target folder + attachments, exposed to the core as
// IFileStore (for plan execution) and IToolHost (for the AI's read-only tools).
// Archives (zip/jar/apk/pk3...) are addressed as "dir/game.zip!/inner/file.png".
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "core/executor.h"
#include "core/agent.h"
#include "core/attachments.h"
#include "core/target.h"
#include "core/context.h"
#include "archive.h"
#include "fsutil.h"

namespace he {

struct AttachmentFile { std::string name, path; heng::AttachmentKind kind = heng::AttachmentKind::Binary; uint64_t size = 0; };

struct SaveResult { bool ok = false; std::string dir, error; std::vector<std::string> files; };
struct InstallResult { bool ok = false; std::string error; std::vector<std::string> installed; int backedUp = 0; };

class Workspace : public heng::IFileStore, public heng::IToolHost {
public:
    // The "root" is the target folder; for a single file/HTML it is that file's folder.
    void setTarget(const heng::Target& t);
    const std::string& root() const { return root_; }
    const std::string& focusFile() const { return focus_; }   // rel path of the picked file (File/Html targets)

    // attachments
    bool addAttachment(const std::string& path, std::string& err);
    void removeAttachment(size_t i);
    void clearAttachments();
    const std::vector<AttachmentFile>& attachments() const { return att_; }

    // heng::IFileStore
    bool readTarget(const std::string& rel, std::vector<uint8_t>& out, std::string& err) override;
    bool readAttachment(const std::string& name, std::vector<uint8_t>& out, std::string& err) override;
    // heng::IToolHost
    bool list(const std::string& dir, bool recursive, size_t maxEntries, std::vector<heng::HostEntry>& out, std::string& err) override;
    bool read(const std::string& path, uint64_t offset, uint64_t len, std::vector<uint8_t>& out, uint64_t& total, std::string& err) override;
    std::vector<std::string> attachmentNames() override;

    // Plain recursive file list for the initial context (no archive expansion).
    std::vector<heng::FileEntry> scanFolder(size_t maxFiles, bool& truncated);

    // Output handling
    SaveResult saveOutputs(const std::map<std::string, std::vector<uint8_t> >& outputs, const std::string& outBase, const std::string& logText);
    static InstallResult installOutputs(const std::string& outputDir, const std::string& targetRoot, bool makeBackups);
    static std::vector<std::string> findBackups(const std::string& targetRoot);
    static int restoreBackups(const std::string& targetRoot, std::string& err);

private:
    bool readFullLocked(const std::string& rel, std::vector<uint8_t>& out, std::string& err);
    ZipReader* zipLocked(const std::string& archiveRel, std::string& err);

    std::mutex mu_;
    std::string root_, focus_;
    std::vector<AttachmentFile> att_;
    std::map<std::string, std::unique_ptr<ZipReader> > zips_;
    std::string cacheKey_; std::vector<uint8_t> cacheData_;   // last fully-read entry (for chunked reads)
};

} // namespace he
