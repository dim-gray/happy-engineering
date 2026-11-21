// theme.h -- Happy Engineering "Vice City" look: neon palette, animated
// widgets, synthwave background, toasts. ImGui only (no GL / GLFW here).
#pragma once
#include "imgui.h"
#include <string>

namespace he { namespace ui {

// ---- palette -----------------------------------------------------------
namespace col {
inline ImU32 rgba(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
inline ImU32 withAlpha(ImU32 c, float a) { ImU32 al = (ImU32)(((c >> 24) & 255) * a); return (c & 0x00FFFFFF) | (al << 24); }
inline ImU32 mix(ImU32 a, ImU32 b, float t) {
    ImVec4 x = ImGui::ColorConvertU32ToFloat4(a), y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}
extern const ImU32 pink, cyan, yellow, purple, orange, green, red, text, muted, panel, panelHi, deep;
}

// ---- global setup --------------------------------------------------------
struct Fonts { ImFont *body = nullptr, *medium = nullptr, *bold = nullptr, *title = nullptr, *logo = nullptr, *mono = nullptr, *small = nullptr; };
extern Fonts fonts;
// Rebuilds the font atlas for the given UI scale. Caller must refresh the GPU texture afterwards.
void loadFonts(float scale);
void applyStyle(float scale);
void setAnimations(bool on);
bool animationsOn();
double time();                           // seconds since start (ImGui time)

// ---- animation helper -------------------------------------------------------
// Smoothly moves a per-id value towards `target` (exponential ease).
float anim(ImGuiID id, float target, float speed = 14.f);
float animKey(const char* key, float target, float speed = 14.f);

// ---- background ---------------------------------------------------------------
void drawBackground(ImVec2 size, float intensity = 1.f);

// ---- widgets --------------------------------------------------------------------
enum class Icon { Workspace, Plan, Memory, Activity, Settings, About, Folder, File, Globe, Chip, Send, Stop, Plus, Trash, Copy, Check, Bolt, Image, Key, Play, Refresh, Eye };
void drawIcon(ImDrawList* dl, Icon ic, ImVec2 center, float size, ImU32 color);

bool button(const char* label, ImVec2 size = ImVec2(0, 0), ImU32 accent = 0, bool filled = false, Icon icon = (Icon)-1);
bool iconButton(const char* id, Icon icon, ImU32 accent, float size = 30.f, const char* tooltip = nullptr);
bool navItem(const char* label, Icon icon, bool selected, float width);
bool segmented(const char* id, const char* const* items, int count, int* sel, float width);
bool chipToggle(const char* label, bool* v, ImU32 accent);
void badge(const char* text, ImU32 color);                      // small rounded label
void statusDot(ImU32 color, bool pulsing);
void shimmerBar(float width, float height, float fraction = -1.f);   // -1 = indeterminate

bool beginPanel(const char* id, ImVec2 size, const char* title = nullptr, ImU32 accent = 0, bool scroll = true);
void endPanel();
void sectionLabel(const char* text, ImU32 accent = 0);
void gradientText(const char* text, ImU32 a, ImU32 b, ImFont* font = nullptr);
void glowText(const char* text, ImU32 color, ImVec2 pos, ImFont* font, float size = 0.f);
void helpMarker(const char* text);
bool inputText(const char* label, std::string* s, const char* hint = nullptr, int flags = 0);
bool inputMultiline(const char* label, std::string* s, ImVec2 size, int flags = 0);

// ---- toasts ---------------------------------------------------------------------
enum class Toast { Info, Success, Warn, Error };
void toast(Toast kind, const std::string& msg, float seconds = 4.f);
void drawToasts(ImVec2 viewport);

// ---- page transition ---------------------------------------------------------------
// Call at the top of a page; returns alpha 0..1 and shifts the cursor for a slide-in.
float pageEnter(int pageId);

}} // namespace
