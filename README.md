# :) Happy Engineering

An AI-driven reverse-engineering and game-modding assistant for **Windows and Linux** (32-bit and 64-bit), with a neon Vice City interface.

You pick a target (a file, a game folder, an HTML file or a running process), tell Happy what you want changed, and it explores the target with **read-only tools** (list / read / search / hex dump / strings, including *inside* `.zip` `.jar` `.apk` `.pk3` archives), then proposes a **plan** you review as a diff before anything is written.

## Features

- **Providers**: Anthropic, OpenAI, OpenRouter, or any OpenAI-compatible endpoint (Ollama, LM Studio, a proxy...).
- **Saved API profiles** (Settings > API profiles): several named profiles, one active at a time. Keys are encrypted with Windows DPAPI on Windows, obfuscated in a `chmod 600` file on Linux (not strong encryption).
- **Editable AI personality** (Settings > AI personality). The tool/plan protocol is appended automatically and cannot be removed.
- **Agent loop**: the AI inspects the target step by step; every tool call is visible in the conversation feed.
- **Plan actions**: `replace_text`, `replace_image` (auto-resized/re-encoded to the original's size and format, also inside archives), `replace_file`, `patch_bytes` (with expected-bytes check), `mem_write` (only to labels you named).
- **Diff review, dry-run check, per-action checkboxes**; results are written to an **Output** folder first. *Install into game* is a separate, confirmed step that keeps `.hebak` backups; *Restore originals* undoes it.
- **Memory scanner** like Cheat Engine (first/next scan, watch list with labels, write, freeze). The AI only sees your labels.
- Attach images, audio, text or any file; drag & drop onto the window.
- **Tools page (decompile / recompile)**: Happy detects installed external tools on Windows and Linux and runs them for you. Decompilers/disassemblers: **Ghidra** (headless, exports C for every function), **radare2**, **Rizin**, **RetDec**, **ILSpy** (.NET), **objdump**. Compilers/assemblers: gcc, g++, clang, clang++, MinGW-w64 (32- and 64-bit .exe), Tiny C, MSVC `cl`, NASM. Each tool has *Path...* (point at the exe or the unpacked folder), *Download* and *Install help*. Ghidra and RetDec need a 64-bit system (the page says so on 32-bit PCs); radare2, Rizin and objdump work everywhere.
- **The AI can use them too**: tools `list_tools`, `decompile` (output saved under `_happy/decomp/`, readable with `read_text`/`search_text`) and `compile_code` (test-builds source the AI wrote into `_happy/build/`). In the Recompile tab you can build, save the binary to the Builds folder, or press *Ask AI to fix* with the compiler errors.
- **Copy from the AI**: every message has a *Copy* button, every code block has *Copy* and (for C/C++/asm) *To Tools*, tool results have *Copy result*, and *Copy chat* copies the whole conversation.

## What it does not do

No DLL injection, hooking, code patching in running processes, DRM / anti-cheat bypass, or help with cheating in online games. Use it on software you have the right to modify.

## Downloads

Builds for `windows-x64`, `windows-x86`, `linux-x64`, `linux-x86` are produced by GitHub Actions on every push (Actions tab > latest run > Artifacts) and attached to GitHub Releases for tags `v*`.

- **Windows**: unzip and run `HappyEngineering.exe` (static, no installer, Windows 7+; 32-bit build works on 32-bit Windows 8).
- **Linux**: `tar xzf`, run `./HappyEngineering`. Needs X11/OpenGL and `libcurl` (`libcurl4` on Debian/Ubuntu).
- Memory scanning on Linux needs ptrace permission (same user, or `sudo`; `kernel.yama.ptrace_scope=0`). A 32-bit build cannot read a 64-bit process.

## Build from source

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build        # unit tests of the portable core
```
Requires CMake 3.16+, a C++17 compiler, and on Linux: `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libcurl4-openssl-dev`.
Windows cross-compile from Linux: `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-x64.cmake` (or `-x86`), `-DHE_BUILD_TESTS=OFF`.
Dependencies (Dear ImGui, GLFW, miniz, stb) are fetched by CMake.

## Layout

```
src/core      portable logic: JSON, PE parser, prompts, agent tools, plan protocol, executor, scanner (unit-tested)
src/app       workspace (folders + archives), image conversion, AI session thread
src/platform  Windows / Linux specifics: HTTP, process memory, secrets, paths
src/ui        Vice City theme, pages, settings window
tools/        icon generator, headless screenshot renderer
```

## Privacy

Everything you ask goes to the provider you configured: target info (file list, small text samples, strings), attachments, memory watch list, and the data returned by the tools the AI runs. Pick targets you are comfortable sending to that provider.

MIT license.
