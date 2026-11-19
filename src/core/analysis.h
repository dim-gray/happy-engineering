// analysis.h -- Happy Enginiring
// Turns raw, already-gathered data (file bytes, HTML text, process/module
// info) into a bounded text block to hand to the AI, plus the fixed
// system prompt describing the assistant's role. This module never touches
// the filesystem or the OS itself -- callers (main.cpp / win layer) read
// the file or enumerate the process and pass the results in. That split is
// what keeps this header portable and unit-testable on any platform.
#pragma once
#include <string>
#include <sstream>
#include <vector>
#include "pe.h"
#include "strings_extract.h"
#include "target.h"

namespace heng {

// The built-in, factory-default "personality"/system prompt. Shown
// pre-filled in the app's editable "Personalitate AI" box so the user can
// see exactly what's being sent and tweak it to their liking; "Reseteaza
// la implicit" in the UI restores this exact text.
inline const char* defaultSystemPrompt() {
    return
        "Esti \"Happy Enginiring\", un asistent de inginerie inversa si modding "
        "pentru programe si jocuri. Primesti informatii deja extrase despre o "
        "tinta aleasa de utilizator: headere PE, importuri, exporturi, siruri de "
        "caractere, continut HTML, lista de fisiere dintr-un folder, module "
        "incarcate, atasamente (imagini, audio, text) si valori din memorie "
        "alese de utilizator. Explica ce face tinta si ce se poate modifica, pe "
        "baza STRICT a datelor primite; cand ceva nu reiese din date, spune clar "
        "ca e o presupunere. Cand utilizatorul cere o modificare, propui un plan "
        "de actiuni concrete (vezi protocolul planului), pe care utilizatorul il "
        "verifica si il aproba inainte sa se intample ceva. Ajuti doar la "
        "modificari legitime: jocuri single-player sau offline, programe proprii, "
        "traduceri, retexturare, modding. Nu generezi chei de licenta, ocolire de "
        "DRM sau anti-cheat, malware sau exploit-uri si nu ajuti la trisat in "
        "jocuri online sau multiplayer. Raspunzi concis, structurat, in limba in "
        "care scrie utilizatorul.";
}

// The fixed plan protocol. It is ALWAYS appended after the (user-editable)
// personality, so editing the personality can never break plan parsing.
inline const char* planProtocolPrompt() {
    return R"HEPLAN(PROTOCOLUL PLANULUI (fix):
Daca utilizatorul cere modificari, explica pe scurt in text, apoi adauga LA FINAL exact un bloc de cod cu eticheta happy-plan, continand JSON valid:
```happy-plan
{"summary":"ce face planul, pe scurt","actions":[ ... ],"need_files":["cale/relativa"]}
```
Tipuri de actiuni ("note" = motivul, optional):
1. {"type":"replace_text","file":"cale/relativa","find":"text exact existent","replace":"text nou","encoding":"utf8","all":true}
   encoding: utf8 | utf16le | ascii. In fisiere binare lungimea nu poate creste (un text mai scurt se completeaza cu 0x00).
2. {"type":"replace_file","file":"cale/relativa","with_attachment":"nume_atasament.ext"}
   Atasamentul trebuie sa aiba aceeasi extensie; la imagini/audio pastreaza aceeasi dimensiune/format.
3. {"type":"patch_bytes","file":"cale/relativa","offset":"0x1A2B","expect":"74 05","write":"EB 05"}
   "expect" = octetii actuali de la offset (include-i mereu daca ii cunosti).
4. {"type":"mem_write","label":"eticheta","value":"99999"}
   DOAR etichete din lista de memorie primita; nu inventa adrese.
Reguli: folosesti doar fisiere din lista primita, cu cai relative separate prin "/", fara "..". Textul din "find" trebuie sa existe exact in continutul sau sirurile primite; daca nu esti sigur, cere fisierul prin "need_files" in loc sa ghicesti. Nu inventa offset-uri, adrese sau continut. Daca lipsesc date, spune ce lipseste si nu trimite plan. Maxim 40 de actiuni, un singur bloc happy-plan.)HEPLAN";
}

// Trims the user-edited system prompt; falls back to the default if it was
// cleared to blank (so an empty box can never silently send zero
// instructions to the model).
inline std::string effectiveSystemPrompt(const std::string& custom) {
    size_t a = custom.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return defaultSystemPrompt();
    size_t b = custom.find_last_not_of(" \t\r\n");
    return custom.substr(a, b - a + 1);
}

// What is actually sent as the system message: the personality (custom or
// default) followed by the fixed plan protocol.
inline std::string fullSystemPrompt(const std::string& customPersonality) {
    return effectiveSystemPrompt(customPersonality) + "\n\n" + planProtocolPrompt();
}

inline std::string truncateWithNote(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "\n...[trunchiat, " + std::to_string(s.size() - maxLen) + " caractere omise]";
}

// ---- File (exe/dll/any binary) -----------------------------------------
inline std::string buildFileContext(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ostringstream o;
    o << "## Fisier tinta\n";
    o << "Cale: " << path << "\n";
    o << "Dimensiune: " << bytes.size() << " octeti\n\n";

    PeInfo pe = parsePe(bytes.data(), bytes.size());
    if (pe.valid) {
        o << "## Header PE\n";
        o << "Arhitectura: " << pe.machineName << (pe.is64 ? " (PE32+)" : " (PE32)") << "\n";
        o << "Tip: " << (pe.isDll ? "DLL" : "EXE") << "\n";
        o << "Subsistem: " << pe.subsystemName << "\n";
        o << "Entry point RVA: 0x" << std::hex << pe.entryPointRva << std::dec << "\n";
        o << "Image base: 0x" << std::hex << pe.imageBase << std::dec << "\n";
        o << "Timestamp link (raw): " << pe.timestamp << "\n";
        o << "Numar sectiuni: " << pe.numberOfSections << "\n\n";

        o << "## Sectiuni\n";
        for (auto& s : pe.sections) {
            o << "- " << s.name << "  VA=0x" << std::hex << s.virtualAddress
              << " VSize=0x" << s.virtualSize << " RawSize=0x" << s.rawSize << std::dec << "\n";
        }
        o << "\n";

        o << "## Importuri (DLL -> cateva functii)\n";
        size_t shown = 0;
        for (auto& imp : pe.imports) {
            if (shown++ >= 40) { o << "... (restul importurilor omise)\n"; break; }
            o << "- " << imp.dll << ": ";
            for (size_t i = 0; i < imp.functions.size() && i < 12; ++i) {
                if (i) o << ", ";
                o << imp.functions[i];
            }
            if (imp.functions.size() > 12) o << ", ...";
            o << "\n";
        }
        o << "\n";

        if (!pe.exports.empty()) {
            o << "## Exporturi\n";
            for (size_t i = 0; i < pe.exports.size() && i < 60; ++i) o << "- " << pe.exports[i] << "\n";
            if (pe.exports.size() > 60) o << "... (restul exporturilor omise)\n";
            o << "\n";
        }
    } else {
        o << "## Header PE\n(nu este un fisier PE recunoscut: " << pe.error << ")\n\n";
    }

    auto strs = extractInterestingStrings(bytes.data(), bytes.size(), 5, 250);
    o << "## Siruri de caractere relevante (esantion, max 250)\n";
    for (auto& s : strs) o << "- " << s << "\n";

    return truncateWithNote(o.str(), 16000);
}

// ---- HTML -----------------------------------------------------------------
inline std::string buildHtmlContext(const std::string& path, const std::string& htmlText) {
    std::ostringstream o;
    o << "## Fisier HTML tinta\n";
    o << "Cale: " << path << "\n";
    o << "Dimensiune: " << htmlText.size() << " caractere\n\n";

    // naive, dependency-free scan for a few interesting tag attributes
    auto collect = [&](const char* attr) {
        std::vector<std::string> found;
        std::string needle = std::string(attr) + "=\"";
        size_t pos = 0;
        while ((pos = htmlText.find(needle, pos)) != std::string::npos && found.size() < 60) {
            size_t start = pos + needle.size();
            size_t end = htmlText.find('"', start);
            if (end == std::string::npos) break;
            found.push_back(htmlText.substr(start, end - start));
            pos = end + 1;
        }
        return found;
    };
    auto srcs = collect("src");
    auto hrefs = collect("href");

    if (!srcs.empty()) {
        o << "## Resurse incarcate (src=...)\n";
        for (auto& s : srcs) o << "- " << s << "\n";
        o << "\n";
    }
    if (!hrefs.empty()) {
        o << "## Legaturi / stiluri (href=...)\n";
        for (auto& h : hrefs) o << "- " << h << "\n";
        o << "\n";
    }

    o << "## Continut HTML (poate fi trunchiat)\n";
    o << truncateWithNote(htmlText, 10000);
    return truncateWithNote(o.str(), 16000);
}

// ---- Process ------------------------------------------------------------
struct ModuleSummary {
    std::string name;
    std::string path;
};

inline std::string buildProcessContext(uint32_t pid, const std::string& processName,
                                        const std::string& mainModulePath,
                                        const std::vector<ModuleSummary>& modules,
                                        const std::string& mainModulePeContext /* from buildFileContext, or empty */) {
    std::ostringstream o;
    o << "## Proces tinta\n";
    o << "PID: " << pid << "\n";
    o << "Nume: " << processName << "\n";
    o << "Executabil principal: " << mainModulePath << "\n\n";

    o << "## Module incarcate (" << modules.size() << ")\n";
    size_t shown = 0;
    for (auto& m : modules) {
        if (shown++ >= 80) { o << "... (restul modulelor omise)\n"; break; }
        o << "- " << m.name << "  (" << m.path << ")\n";
    }
    o << "\n";

    if (!mainModulePeContext.empty()) {
        o << "## Analiza statica a executabilului principal\n";
        o << mainModulePeContext << "\n";
    }

    return truncateWithNote(o.str(), 16000);
}

// Combines the user's free-text instructions with the gathered context into
// the final prompt sent as the "user" message/content.
inline std::string buildUserPrompt(const std::string& instructions, const std::string& context) {
    std::ostringstream o;
    o << "# Instructiuni de la utilizator\n" << (instructions.empty() ? "(niciuna -- fa o analiza generala)" : instructions) << "\n\n";
    o << "# Context extras despre tinta\n" << context;
    return o.str();
}

} // namespace heng
