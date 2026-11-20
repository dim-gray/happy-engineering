// executor.h -- Happy Enginiring
// Runs a reviewed edit plan. File actions are applied to in-memory copies of
// the target's files (read through an IFileStore) and returned as "outputs";
// the caller writes those into the Output folder, so the original game files
// are never modified by this code. Memory actions go through IMemory and may
// only touch addresses from the user's own watch list.
//
// The same function serves "Verifica" (dry run: nothing is written to
// memory, outputs are simply not saved) and "Aplica".
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include "actions.h"
#include "patcher.h"
#include "scanner.h"

namespace heng {

class IFileStore {
public:
    virtual ~IFileStore() {}
    // A file inside the target folder, by relative path with '/' separators.
    virtual bool readTarget(const std::string& rel, std::vector<uint8_t>& out, std::string& err) = 0;
    // An attachment by its file name.
    virtual bool readAttachment(const std::string& name, std::vector<uint8_t>& out, std::string& err) = 0;
};

// Re-encodes an image so it can drop into a game file: same pixel size and
// same container format as the original. Implemented in the app layer
// (stb_image); the core only defines the contract so it stays testable.
class IImageConverter {
public:
    virtual ~IImageConverter() {}
    // `orig` = the game's current image file, `src` = the user's replacement.
    // On success `out` holds the converted file; `note` is an optional remark.
    virtual bool convert(const std::vector<uint8_t>& orig, const std::string& origName,
                         const std::vector<uint8_t>& src, const std::string& fit,
                         std::vector<uint8_t>& out, std::string& note, std::string& err) = 0;
};

struct ActionResult {
    bool ok = false;        // valid / succeeded
    bool skipped = false;   // not selected, so not run
    std::string message;    // what happened (or why it failed)
    std::string warning;    // non-fatal remark (e.g. texture size differs)
};

struct ExecutionReport {
    std::vector<ActionResult> results;                    // one per plan action, same order
    std::map<std::string, std::vector<uint8_t>> outputs;  // relative path -> modified bytes
    size_t okCount = 0, failCount = 0, skipCount = 0;
};

inline ActionResult executeMemWrite(const PlanAction& a, const std::vector<WatchEntry>& watch,
                                    IMemory* mem, bool applyNow) {
    ActionResult r;
    const WatchEntry* w = findWatch(watch, a.label);
    if (!w) {
        r.message = "label '" + a.label + "' is not in your memory watch list (the AI can only use labels you created)";
        return r;
    }
    std::vector<uint8_t> bytes;
    std::string err;
    if (!parseValue(w->type, a.value, bytes, err)) { r.message = err; return r; }
    char addr[32];
    std::snprintf(addr, sizeof(addr), "0x%llX", static_cast<unsigned long long>(w->address));
    std::string what = a.label + " (" + addr + ", " + valueTypeName(w->type) + ")";
    if (!applyNow) {
        r.ok = true;
        r.message = "will write " + a.value + " to " + what;
        return r;
    }
    if (!mem) { r.message = "no process is attached; open the Memory page and pick the target process"; return r; }
    std::vector<uint8_t> before;
    bool hadBefore = readValue(*mem, w->address, w->type, before);
    if (!mem->write(w->address, bytes.data(), bytes.size())) {
        r.message = "writing to " + what + " failed (protected memory or the process exited)";
        return r;
    }
    r.ok = true;
    r.message = "wrote " + a.value + " to " + what +
                (hadBefore ? " (before: " + formatValue(w->type, before.data()) + ")" : std::string());
    return r;
}

inline ExecutionReport runPlan(const Plan& plan, const std::vector<bool>& selected, IFileStore& store,
                               const std::vector<WatchEntry>& watch, IMemory* mem, bool applyMemory, IImageConverter* images = nullptr) {
    ExecutionReport rep;
    rep.results.resize(plan.actions.size());
    std::map<std::string, std::vector<uint8_t> > work;
    std::set<std::string> touched;

    for (size_t i = 0; i < plan.actions.size(); ++i) {
        const PlanAction& a = plan.actions[i];
        ActionResult& r = rep.results[i];
        if (i >= selected.size() || !selected[i]) { r.skipped = true; r.message = "skipped (unchecked)"; continue; }
        if (!a.error.empty()) { r.message = "invalid: " + a.error; continue; }

        if (a.type == ActionType::MemWrite) { r = executeMemWrite(a, watch, mem, applyMemory); continue; }

        // --- file actions: stage the target file in memory ---
        std::map<std::string, std::vector<uint8_t> >::iterator it = work.find(a.file);
        if (it == work.end()) {
            std::vector<uint8_t> bytes;
            std::string err;
            if (!store.readTarget(a.file, bytes, err)) { r.message = "cannot read " + a.file + ": " + err; continue; }
            it = work.insert(std::make_pair(a.file, bytes)).first;
        }
        std::vector<uint8_t>& buf = it->second;

        if (a.type == ActionType::ReplaceText) {
            PatchOutcome o = applyReplaceText(buf, a, a.file);
            r.ok = o.ok; r.message = o.message;
        } else if (a.type == ActionType::PatchBytes) {
            PatchOutcome o = applyPatchBytes(buf, a);
            r.ok = o.ok; r.message = o.message;
        } else if (a.type == ActionType::ReplaceFile) {
            std::vector<uint8_t> attach;
            std::string err;
            if (!store.readAttachment(a.attachment, attach, err)) {
                r.message = "cannot read attachment '" + a.attachment + "': " + err;
            } else {
                ReplaceCheck c = checkReplaceFile(a.file, buf, a.attachment, attach);
                if (!c.ok) {
                    r.message = c.message;
                } else {
                    buf.swap(attach);
                    r.ok = true;
                    r.message = "the file will be replaced with " + a.attachment;
                    r.warning = c.message;
                }
            }
        } else if (a.type == ActionType::ReplaceImage) {
            std::vector<uint8_t> attach, converted;
            std::string err, note;
            if (!images) {
                r.message = "image conversion is not available in this build";
            } else if (!store.readAttachment(a.attachment, attach, err)) {
                r.message = "cannot read attachment '" + a.attachment + "': " + err;
            } else if (!images->convert(buf, a.file, attach, a.fit, converted, note, err)) {
                r.message = err;
            } else {
                buf.swap(converted);
                r.ok = true;
                r.message = "image re-textured from " + a.attachment;
                r.warning = note;
            }
        } else {
            r.message = "unknown action type";
        }
        if (r.ok) touched.insert(a.file);
    }

    for (std::set<std::string>::const_iterator t = touched.begin(); t != touched.end(); ++t)
        rep.outputs[*t] = work[*t];
    for (size_t i = 0; i < rep.results.size(); ++i) {
        if (rep.results[i].skipped) ++rep.skipCount;
        else if (rep.results[i].ok) ++rep.okCount;
        else ++rep.failCount;
    }
    return rep;
}

} // namespace heng
