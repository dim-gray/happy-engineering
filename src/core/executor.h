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
        r.message = "eticheta '" + a.label + "' nu exista in lista de memorie (AI-ul poate folosi doar etichete create de tine)";
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
        r.message = "va scrie " + a.value + " in " + what;
        return r;
    }
    if (!mem) { r.message = "nu este conectat niciun proces; deschide fereastra Memorie pentru procesul tinta"; return r; }
    std::vector<uint8_t> before;
    bool hadBefore = readValue(*mem, w->address, w->type, before);
    if (!mem->write(w->address, bytes.data(), bytes.size())) {
        r.message = "scrierea in " + what + " a esuat (memorie protejata sau proces inchis)";
        return r;
    }
    r.ok = true;
    r.message = "scris " + a.value + " in " + what +
                (hadBefore ? " (inainte: " + formatValue(w->type, before.data()) + ")" : std::string());
    return r;
}

inline ExecutionReport runPlan(const Plan& plan, const std::vector<bool>& selected, IFileStore& store,
                               const std::vector<WatchEntry>& watch, IMemory* mem, bool applyMemory) {
    ExecutionReport rep;
    rep.results.resize(plan.actions.size());
    std::map<std::string, std::vector<uint8_t> > work;
    std::set<std::string> touched;

    for (size_t i = 0; i < plan.actions.size(); ++i) {
        const PlanAction& a = plan.actions[i];
        ActionResult& r = rep.results[i];
        if (i >= selected.size() || !selected[i]) { r.skipped = true; r.message = "omisa (debifata)"; continue; }
        if (!a.error.empty()) { r.message = "invalida: " + a.error; continue; }

        if (a.type == ActionType::MemWrite) { r = executeMemWrite(a, watch, mem, applyMemory); continue; }

        // --- file actions: stage the target file in memory ---
        std::map<std::string, std::vector<uint8_t> >::iterator it = work.find(a.file);
        if (it == work.end()) {
            std::vector<uint8_t> bytes;
            std::string err;
            if (!store.readTarget(a.file, bytes, err)) { r.message = "nu pot citi " + a.file + ": " + err; continue; }
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
                r.message = "nu pot citi atasamentul '" + a.attachment + "': " + err;
            } else {
                ReplaceCheck c = checkReplaceFile(a.file, buf, a.attachment, attach);
                if (!c.ok) {
                    r.message = c.message;
                } else {
                    buf.swap(attach);
                    r.ok = true;
                    r.message = "fisierul va fi inlocuit cu " + a.attachment;
                    r.warning = c.message;
                }
            }
        } else {
            r.message = "tip de actiune necunoscut";
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
