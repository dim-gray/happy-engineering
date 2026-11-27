# Happy Engineering - project status

_Last updated: 2026-11-27. Read this first when resuming work; it replaces re-reading the old chat._

Repo: https://github.com/dim-gray/happy-engineering (public, branch `main`).
Releases: `v1.0.0` published (has the "buttons don't respond" bug). Next release should be **v1.2.0** (create it in the GitHub web UI: Releases -> Draft new release -> new tag on `main`; the workflow attaches the 4 downloads).

## What it is
AI-driven reverse-engineering / game-modding desktop tool (like "R.E.A"), C++17 + Dear ImGui + GLFW, Vice City / synthwave UI.
Providers: OpenAI, OpenRouter, Anthropic, custom OpenAI-compatible. Free/Pro tier label, editable AI personality, saved API profiles.
Targets: single file, EXE/DLL, HTML, game folder (incl. zip archives), running process. Attachments (images, audio, any file).
The AI works through a read-only tool loop, then proposes a plan the user reviews; Apply writes only to the Output folder; installing into the game is a separate step with `.hebak` backups.

## Done (all on main, CI green on the last built commit)
- Vice City UI, Settings window (API profiles, personality, safety), app icon (":)" on vanilla), 5 -> 6 pages: Workspace, Plan, Memory, Tools, Output, About.
- Plan actions: replace_text, replace_image, replace_file, patch_bytes, mem_write (Cheat-Engine-style process memory editing/scanning).
- **Tools page**: detects and runs Ghidra (headless), radare2, Rizin, RetDec, ILSpy, objdump; compilers gcc/g++/clang/MinGW-w64 32+64/tcc/MSVC/NASM. Recompile tab (build, Save build, Ask AI to fix).
- **Automatic install** (UI button and AI tool `install_tool`): official hosts only (GitHub, Adoptium, nasm.us), https, SHA-256 when published, zip-slip-safe, asks the user (or auto-approve in Settings). Installs into `<config>/tools/<package>`. Ghidra pulls a Temurin JDK and gets `JAVA_HOME_OVERRIDE`.
- **AI tools in chat**: `list_tools`, `install_tool`, `decompile`, `compile_code` + the older read-only ones. Decompiler output is readable at `_happy/decomp/...`, builds at `_happy/build/...`.
- **Copy from the AI**: per message, per code block (+ "To Tools"), tool results, whole chat.
- **Chat commands** (local, no API key needed): `/help /tools /install <id> /decompile <file> [fn] /build [name] [32|64]`. A full path typed by the user in a message lets the AI decompile exactly that file (`Workspace::grantPaths`). Chat no longer needs a target.
  - NOTE: this last item (chat commands + path grants) was written after the last CI-verified commit: check CI after pushing.
- CI: `.github/workflows/build.yml` builds Linux x64, Linux x86 (i386 docker), Windows x64 + x86 (mingw cross), runs tests, publishes logs to the `ci-logs` branch, and creates the release on `v*` tags.

## Layout
- `src/core/` portable header-only logic (tested): json, pe, strings, target, settings, provider, chat, profiles (config incl. tool paths), agent (tool protocol), actions/executor/patcher, scanner, **toolkit.h** (tool catalog, locating, command lines, slash/path parsing), **install.h** (install catalog, host allowlist, release-asset pickers), sha256.h.
- `src/app/` workspace (files, archives, tool host for the AI), ai_session (agent thread), process (run programs), **toolbox** (detect/run/compile/decompile) + **installer.cpp** (download/unpack/approval), archive, imgconv, fsutil.
- `src/platform/` Windows (WinHTTP, DPAPI, Toolhelp, memory API) and POSIX (libcurl, /proc) implementations of `platform.h`.
- `src/ui/` ImGui pages (`page_workspace/plan/memory/tools.cpp`), `app.cpp`, `theme.cpp`, `filebrowser.cpp`. `src/main.cpp` GLFW entry.
- `test/` test_core, test_edit, test_agent, test_workspace, test_toolkit, test_install (all pass: `cmake -S . -B build -DHE_BUILD_GUI=OFF && cmake --build build && ctest`).
- `tools/shot/` headless software-rasterizer screenshot tool for the UI (not in CMake; needs a local build script, rebuild after header changes).

## Known limits / honesty
- GUI, real network downloads, Ghidra runs, process memory access and Windows file I/O have only been verified by compilation, Linux unit tests, fake-network tests and headless screenshots, never on a real Windows screen. Expect rough edges.
- The user's PC is **Windows 8, 32-bit**: Ghidra/RetDec/JDK need 64-bit; radare2, NASM and WinLibs i686 GCC are the useful installs. WinLibs needs the UCRT (Win 8.1 needs KB2999226).
- Linux compilers/radare2/NASM are not auto-installed (need sudo); Happy shows the apt command instead. RetDec has no Windows zip upstream.
- Decompiled code is approximate; expect cleanup before it recompiles.

## Working notes for future sessions
- Pushing tags from the cloud sandbox is refused (403) - the user creates releases in the GitHub web UI.
- `git push` works with `git -c push.negotiate=true push origin HEAD`. Commit trailers: `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: ...`.
- CI status without a token: `https://api.github.com/repos/dim-gray/happy-engineering/actions/runs?per_page=1` (+ `/runs/<id>/jobs`).
- GUI sources cannot be compiled locally (no X11/GLFW headers); only CI proves them. The sandbox can be reset between sessions: uncommitted work is lost, so commit early.
- Versioning: MAJOR.MINOR.PATCH (feature = minor, fix = patch).

## Ideas not done yet
- ILSpy/jadx auto-install, Linux package-manager helper, code-signing, installer package, auto-update check, in-chat inline decompile viewer with syntax highlighting, function-level "explain this" buttons.
