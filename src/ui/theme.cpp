#include "theme.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

extern const unsigned char font_regular[]; extern const std::size_t font_regular_size;
extern const unsigned char font_medium[];  extern const std::size_t font_medium_size;
extern const unsigned char font_bold[];    extern const std::size_t font_bold_size;
extern const unsigned char font_mono[];    extern const std::size_t font_mono_size;

namespace he { namespace ui {

namespace col {
const ImU32 pink = IM_COL32(255, 62, 165, 255), cyan = IM_COL32(40, 241, 232, 255), yellow = IM_COL32(255, 228, 94, 255),
            purple = IM_COL32(155, 92, 255, 255), orange = IM_COL32(255, 122, 69, 255), green = IM_COL32(92, 255, 177, 255),
            red = IM_COL32(255, 77, 109, 255), text = IM_COL32(244, 233, 255, 255), muted = IM_COL32(165, 143, 200, 255),
            panel = IM_COL32(20, 7, 48, 222), panelHi = IM_COL32(48, 20, 92, 210), deep = IM_COL32(12, 4, 30, 255);
}
using namespace col;

Fonts fonts;
static bool g_anim = true;
void setAnimations(bool on) { g_anim = on; }
bool animationsOn() { return g_anim; }
double time() { return ImGui::GetTime(); }
static ImU32 fa(ImU32 c) { return withAlpha(c, ImGui::GetStyle().Alpha); }
static float easeOut(float t) { t = std::min(1.f, std::max(0.f, t)); return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

// ---------------------------------------------------------------- fonts / style
void loadFonts(float s) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x024F, 0x2010, 0x2027, 0x2030, 0x203A, 0x2190, 0x2193, 0};
    ImFontConfig c; c.FontDataOwnedByAtlas = false; c.OversampleH = 2; c.OversampleV = 2;
    auto add = [&](const unsigned char* d, std::size_t n, float px) {
        return io.Fonts->AddFontFromMemoryTTF((void*)d, (int)n, px * s, &c, ranges);
    };
    fonts.body = add(font_regular, font_regular_size, 17.f);
    fonts.small = add(font_regular, font_regular_size, 14.f);
    fonts.medium = add(font_medium, font_medium_size, 17.f);
    fonts.bold = add(font_bold, font_bold_size, 17.f);
    fonts.title = add(font_bold, font_bold_size, 26.f);
    fonts.logo = add(font_bold, font_bold_size, 64.f);
    fonts.mono = add(font_mono, font_mono_size, 15.f);
    io.FontDefault = fonts.body;
    io.Fonts->Build();
}

void applyStyle(float s) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowRounding = 14 * s; st.ChildRounding = 14 * s; st.FrameRounding = 9 * s; st.PopupRounding = 12 * s;
    st.ScrollbarRounding = 10 * s; st.GrabRounding = 8 * s; st.TabRounding = 9 * s;
    st.WindowPadding = ImVec2(16 * s, 14 * s); st.FramePadding = ImVec2(12 * s, 8 * s);
    st.ItemSpacing = ImVec2(10 * s, 9 * s); st.ItemInnerSpacing = ImVec2(8 * s, 6 * s);
    st.ScrollbarSize = 11 * s; st.GrabMinSize = 14 * s;
    st.WindowBorderSize = 1; st.ChildBorderSize = 0; st.FrameBorderSize = 1; st.PopupBorderSize = 1;
    st.CellPadding = ImVec2(10 * s, 7 * s);
    ImVec4* c = st.Colors;
    auto V = [](ImU32 x) { return ImGui::ColorConvertU32ToFloat4(x); };
    c[ImGuiCol_Text] = V(text); c[ImGuiCol_TextDisabled] = V(muted);
    c[ImGuiCol_WindowBg] = V(IM_COL32(18, 6, 44, 245)); c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = V(IM_COL32(26, 9, 60, 250));
    c[ImGuiCol_Border] = V(IM_COL32(255, 62, 165, 90)); c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = V(IM_COL32(10, 3, 28, 200)); c[ImGuiCol_FrameBgHovered] = V(IM_COL32(40, 14, 84, 230)); c[ImGuiCol_FrameBgActive] = V(IM_COL32(58, 20, 110, 240));
    c[ImGuiCol_TitleBg] = V(IM_COL32(18, 6, 44, 255)); c[ImGuiCol_TitleBgActive] = V(IM_COL32(40, 12, 80, 255)); c[ImGuiCol_TitleBgCollapsed] = V(IM_COL32(18, 6, 44, 255));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_ScrollbarGrab] = V(IM_COL32(155, 92, 255, 120));
    c[ImGuiCol_ScrollbarGrabHovered] = V(IM_COL32(255, 62, 165, 180)); c[ImGuiCol_ScrollbarGrabActive] = V(pink);
    c[ImGuiCol_CheckMark] = V(cyan); c[ImGuiCol_SliderGrab] = V(pink); c[ImGuiCol_SliderGrabActive] = V(orange);
    c[ImGuiCol_Button] = V(IM_COL32(60, 22, 110, 200)); c[ImGuiCol_ButtonHovered] = V(IM_COL32(255, 62, 165, 150)); c[ImGuiCol_ButtonActive] = V(IM_COL32(255, 62, 165, 220));
    c[ImGuiCol_Header] = V(IM_COL32(155, 92, 255, 70)); c[ImGuiCol_HeaderHovered] = V(IM_COL32(255, 62, 165, 90)); c[ImGuiCol_HeaderActive] = V(IM_COL32(255, 62, 165, 140));
    c[ImGuiCol_Separator] = V(IM_COL32(155, 92, 255, 70));
    c[ImGuiCol_Tab] = V(IM_COL32(40, 14, 84, 200)); c[ImGuiCol_TabHovered] = V(IM_COL32(255, 62, 165, 150)); c[ImGuiCol_TabSelected] = V(IM_COL32(255, 62, 165, 200));
    c[ImGuiCol_TableHeaderBg] = V(IM_COL32(40, 14, 84, 230)); c[ImGuiCol_TableBorderStrong] = V(IM_COL32(155, 92, 255, 80)); c[ImGuiCol_TableBorderLight] = V(IM_COL32(155, 92, 255, 40));
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0); c[ImGuiCol_TableRowBgAlt] = V(IM_COL32(255, 255, 255, 8));
    c[ImGuiCol_TextSelectedBg] = V(IM_COL32(40, 241, 232, 90));
    c[ImGuiCol_ModalWindowDimBg] = V(IM_COL32(6, 0, 20, 170));
    c[ImGuiCol_PlotHistogram] = V(pink);
}

// ---------------------------------------------------------------- animation
float anim(ImGuiID id, float target, float speed) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    float cur = st->GetFloat(id, target);
    if (!g_anim) { st->SetFloat(id, target); return target; }
    float dt = ImGui::GetIO().DeltaTime;
    cur += (target - cur) * (1.f - std::exp(-speed * dt));
    if (std::fabs(target - cur) < 0.001f) cur = target;
    st->SetFloat(id, cur);
    return cur;
}
float animKey(const char* key, float target, float speed) { return anim(ImGui::GetID(key), target, speed); }

// ---------------------------------------------------------------- background
static float frac(float x) { return x - std::floor(x); }
static float hash1(float n) { return frac(std::sin(n * 12.9898f) * 43758.5453f); }

void drawBackground(ImVec2 sz, float k) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float W = sz.x, H = sz.y, hz = H * 0.60f;
    float t = g_anim ? (float)ImGui::GetTime() : 2.0f;
    auto C = [&](int r, int g, int b, float a = 1.f) { return IM_COL32(r, g, b, (int)(255 * a * k + 255 * (1.f - k) * 0.f)); };
    // sky gradient (3 bands)
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, hz * 0.55f), IM_COL32(10, 3, 32, 255), IM_COL32(10, 3, 32, 255), IM_COL32(58, 12, 96, 255), IM_COL32(58, 12, 96, 255));
    dl->AddRectFilledMultiColor(ImVec2(0, hz * 0.55f), ImVec2(W, hz), IM_COL32(58, 12, 96, 255), IM_COL32(58, 12, 96, 255), IM_COL32(220, 52, 120, 255), IM_COL32(220, 52, 120, 255));
    dl->AddRectFilledMultiColor(ImVec2(0, hz * 0.88f), ImVec2(W, hz), IM_COL32(220, 52, 120, 0), IM_COL32(220, 52, 120, 0), IM_COL32(255, 140, 70, 200), IM_COL32(255, 140, 70, 200));
    // stars
    for (int i = 0; i < 90; ++i) {
        float x = hash1(i * 3.1f) * W, y = hash1(i * 7.7f + 1.f) * hz * 0.62f;
        float tw = 0.5f + 0.5f * std::sin(t * (0.7f + hash1(i + 9.f) * 1.6f) + i);
        float r = 0.7f + hash1(i + 40.f) * 1.3f;
        dl->AddCircleFilled(ImVec2(x, y), r, IM_COL32(255, 240, 255, (int)(40 + 150 * tw * k)));
    }
    // sun with scrolling slits
    float sr = std::min(W, H) * 0.20f, sx = W * 0.5f, sy = hz - sr * 0.28f;
    for (int g = 3; g >= 1; --g) dl->AddCircleFilled(ImVec2(sx, sy), sr * (1.f + 0.18f * g), IM_COL32(255, 90, 150, (int)(10 * k)), 64);
    const int slices = 56;
    for (int i = 0; i < slices; ++i) {
        float y0 = sy - sr + (2 * sr) * i / slices, y1 = sy - sr + (2 * sr) * (i + 1) / slices;
        float u = (float)i / slices;                                // 0 top .. 1 bottom
        float cut = std::max(0.f, (u - 0.42f)) * 1.0f;              // slits grow towards the bottom
        float ph = frac(u * 9.f - t * 0.05f);
        if (cut > 0.f && ph < cut * 0.9f) continue;
        float dy = (y0 + y1) * 0.5f - sy, hw = std::sqrt(std::max(0.f, sr * sr - dy * dy));
        if (y0 > hz) continue;
        ImU32 a = col::mix(yellow, IM_COL32(255, 70, 160, 255), u);
        dl->AddRectFilled(ImVec2(sx - hw, y0), ImVec2(sx + hw, std::min(y1, hz)), withAlpha(a, 0.95f * k));
    }
    // skyline silhouette
    float bx = 0; int bi = 0;
    while (bx < W) {
        float bw = 18 + hash1(bi * 1.7f) * 46, bh = 14 + hash1(bi * 5.3f + 2.f) * H * 0.07f;
        dl->AddRectFilled(ImVec2(bx, hz - bh), ImVec2(bx + bw, hz + 1), IM_COL32(24, 6, 48, 255));
        for (int wy = 0; wy < (int)(bh / 9) - 1; ++wy) for (int wx = 0; wx < (int)(bw / 9); ++wx) {
            if (hash1(bi * 31.f + wy * 7.f + wx * 3.f) > 0.82f) {
                bool pinkW = hash1(bi + wx + wy * 5.f) > 0.5f;
                dl->AddRectFilled(ImVec2(bx + 4 + wx * 9, hz - bh + 5 + wy * 9), ImVec2(bx + 7 + wx * 9, hz - bh + 8 + wy * 9), pinkW ? IM_COL32(255, 62, 165, 150) : IM_COL32(40, 241, 232, 140));
            }
        }
        bx += bw + 2; ++bi;
    }
    // floor + grid
    dl->AddRectFilledMultiColor(ImVec2(0, hz), ImVec2(W, H), IM_COL32(40, 8, 70, 255), IM_COL32(40, 8, 70, 255), IM_COL32(8, 2, 24, 255), IM_COL32(8, 2, 24, 255));
    float vx = W * 0.5f;
    for (int i = -26; i <= 26; ++i) {
        float xb = vx + i * W * 0.11f;
        float a = (0.55f - std::fabs(i) * 0.012f) * k;
        dl->AddLine(ImVec2(vx + i * 6.f, hz), ImVec2(xb, H), IM_COL32(255, 62, 165, (int)(255 * std::max(0.05f, a) * 0.8f)), 1.4f);
    }
    for (int k2 = 0; k2 < 16; ++k2) {
        float u = frac((k2 + t * 0.35f) / 16.f);
        float y = hz + (H - hz) * (u * u * u * 0.85f + u * 0.15f);
        dl->AddLine(ImVec2(0, y), ImVec2(W, y), IM_COL32(40, 241, 232, (int)(150 * u * k)), 1.2f);
    }
    dl->AddRectFilled(ImVec2(0, hz - 1.5f), ImVec2(W, hz + 1.5f), IM_COL32(255, 160, 220, (int)(200 * k)));
    // readability veil so panels/text stay crisp
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(8, 0, 24, (int)(70 * (1.f - 0.3f * k))));
    (void)C;
}

// ---------------------------------------------------------------- icons
void drawIcon(ImDrawList* dl, Icon ic, ImVec2 c, float s, ImU32 col_) {
    float r = s * 0.5f, th = std::max(1.6f, s * 0.09f);
    auto L = [&](float x0, float y0, float x1, float y1) { dl->AddLine(ImVec2(c.x + x0 * r, c.y + y0 * r), ImVec2(c.x + x1 * r, c.y + y1 * r), col_, th); };
    switch (ic) {
        case Icon::Workspace:
            dl->AddRect(ImVec2(c.x - r * .8f, c.y - r * .8f), ImVec2(c.x + r * .8f, c.y + r * .8f), col_, 3.f, 0, th);
            L(-.8f, -.2f, .8f, -.2f); L(-.2f, -.2f, -.2f, .8f); break;
        case Icon::Plan:
            for (int i = 0; i < 3; ++i) { float y = -.6f + i * .6f; dl->AddCircleFilled(ImVec2(c.x - r * .7f, c.y + y * r), th * .8f, col_); L(-.3f, y, .85f, y); } break;
        case Icon::Memory:
            dl->AddRect(ImVec2(c.x - r * .55f, c.y - r * .55f), ImVec2(c.x + r * .55f, c.y + r * .55f), col_, 2.f, 0, th);
            for (int i = -1; i <= 1; ++i) { L(i * .35f, -.55f, i * .35f, -.95f); L(i * .35f, .55f, i * .35f, .95f); L(-.55f, i * .35f, -.95f, i * .35f); L(.55f, i * .35f, .95f, i * .35f); } break;
        case Icon::Activity:
            L(-.95f, .1f, -.5f, .1f); L(-.5f, .1f, -.25f, -.8f); L(-.25f, -.8f, .1f, .8f); L(.1f, .8f, .35f, .1f); L(.35f, .1f, .95f, .1f); break;
        case Icon::Settings:
            dl->AddCircle(c, r * .38f, col_, 16, th);
            for (int i = 0; i < 8; ++i) { float a = i * 0.7853982f; L(std::cos(a) * .62f, std::sin(a) * .62f, std::cos(a) * .92f, std::sin(a) * .92f); }
            dl->AddCircle(c, r * .66f, col_, 24, th); break;
        case Icon::About:
            dl->AddCircle(c, r * .9f, col_, 24, th); dl->AddCircleFilled(ImVec2(c.x, c.y - r * .4f), th * .9f, col_); L(0, -.05f, 0, .55f); break;
        case Icon::Folder:
            dl->AddRect(ImVec2(c.x - r * .9f, c.y - r * .5f), ImVec2(c.x + r * .9f, c.y + r * .7f), col_, 3.f, 0, th); L(-.9f, -.5f, -.35f, -.5f); L(-.35f, -.5f, -.1f, -.8f); break;
        case Icon::File:
            dl->AddRect(ImVec2(c.x - r * .6f, c.y - r * .85f), ImVec2(c.x + r * .6f, c.y + r * .85f), col_, 3.f, 0, th); L(-.3f, -.2f, .3f, -.2f); L(-.3f, .2f, .3f, .2f); break;
        case Icon::Globe:
            dl->AddCircle(c, r * .85f, col_, 24, th); dl->AddEllipse(c, ImVec2(r * .4f, r * .85f), col_, 0.f, 20, th); L(-.85f, 0, .85f, 0); break;
        case Icon::Chip:
            drawIcon(dl, Icon::Memory, c, s, col_); break;
        case Icon::Send:
            L(-.8f, -.7f, .9f, 0); L(.9f, 0, -.8f, .7f); L(-.8f, .7f, -.4f, 0); L(-.4f, 0, -.8f, -.7f); break;
        case Icon::Stop: dl->AddRectFilled(ImVec2(c.x - r * .55f, c.y - r * .55f), ImVec2(c.x + r * .55f, c.y + r * .55f), col_, 2.f); break;
        case Icon::Plus: L(-.7f, 0, .7f, 0); L(0, -.7f, 0, .7f); break;
        case Icon::Trash: L(-.7f, -.5f, .7f, -.5f); L(-.5f, -.5f, -.4f, .8f); L(.5f, -.5f, .4f, .8f); L(-.4f, .8f, .4f, .8f); L(-.2f, -.5f, -.2f, -.8f); L(-.2f, -.8f, .2f, -.8f); L(.2f, -.8f, .2f, -.5f); break;
        case Icon::Copy:
            dl->AddRect(ImVec2(c.x - r * .8f, c.y - r * .5f), ImVec2(c.x + r * .3f, c.y + r * .85f), col_, 2.f, 0, th);
            dl->AddRect(ImVec2(c.x - r * .3f, c.y - r * .85f), ImVec2(c.x + r * .8f, c.y + r * .5f), col_, 2.f, 0, th); break;
        case Icon::Check: L(-.8f, .05f, -.25f, .65f); L(-.25f, .65f, .85f, -.6f); break;
        case Icon::Bolt: L(.25f, -.95f, -.45f, .1f); L(-.45f, .1f, .1f, .1f); L(.1f, .1f, -.25f, .95f); break;
        case Icon::Image:
            dl->AddRect(ImVec2(c.x - r * .85f, c.y - r * .65f), ImVec2(c.x + r * .85f, c.y + r * .65f), col_, 3.f, 0, th);
            L(-.85f, .45f, -.3f, -.1f); L(-.3f, -.1f, .15f, .35f); L(.15f, .35f, .4f, .1f); L(.4f, .1f, .85f, .5f); dl->AddCircleFilled(ImVec2(c.x + r * .4f, c.y - r * .25f), th, col_); break;
        case Icon::Key:
            dl->AddCircle(ImVec2(c.x - r * .4f, c.y), r * .4f, col_, 16, th); L(-.0f, 0, .9f, 0); L(.6f, 0, .6f, .35f); L(.9f, 0, .9f, .3f); break;
        case Icon::Tools:   // wrench-like: handle + open jaw
            L(-.85f, .85f, .25f, -.25f); L(-.55f, .95f, .35f, -.05f);
            dl->PathArcTo(ImVec2(c.x + r * .5f, c.y - r * .5f), r * .5f, 0.9f, 5.4f, 14); dl->PathStroke(col_, 0, th); break;
        case Icon::Play: dl->AddTriangleFilled(ImVec2(c.x - r * .5f, c.y - r * .7f), ImVec2(c.x - r * .5f, c.y + r * .7f), ImVec2(c.x + r * .75f, c.y), col_); break;
        case Icon::Refresh:
            dl->PathArcTo(c, r * .7f, 0.6f, 5.6f, 20); dl->PathStroke(col_, 0, th); L(.62f, -.1f, .8f, -.6f); L(.62f, -.1f, .2f, -.25f); break;
        case Icon::Eye:
            dl->AddEllipse(c, ImVec2(r * .9f, r * .55f), col_, 0.f, 20, th); dl->AddCircleFilled(c, r * .25f, col_); break;
    }
}

// ---------------------------------------------------------------- widgets
static void glowRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 c, float strength) {
    for (int i = 3; i >= 1; --i) {
        float e = i * 2.2f;
        dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), withAlpha(c, strength * (0.10f / i) * 3.f), rounding + e, 0, 2.f);
    }
}

bool button(const char* label, ImVec2 size, ImU32 accent, bool filled, Icon icon) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    if (!accent) accent = pink;
    ImGuiID id = w->GetID(label);
    ImVec2 ts = ImGui::CalcTextSize(label, nullptr, true);
    ImGuiStyle& st = ImGui::GetStyle();
    float iconW = (int)icon >= 0 ? ts.y + 8 : 0;
    ImVec2 sz(size.x > 0 ? size.x : ts.x + iconW + st.FramePadding.x * 2.4f, size.y > 0 ? size.y : ts.y + st.FramePadding.y * 2.f);
    ImVec2 p = w->DC.CursorPos;
    ImRect bb(p, ImVec2(p.x + sz.x, p.y + sz.y));
    ImGui::ItemSize(bb, st.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov, held; bool pressed = ImGui::ButtonBehavior(bb, id, &hov, &held);
    float h = anim(id, hov ? 1.f : 0.f, 16.f), d = anim(id + 1, held ? 1.f : 0.f, 30.f);
    bool dis = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    ImDrawList* dl = w->DrawList;
    ImVec2 a = bb.Min, b = bb.Max; a.y += d * 1.5f; b.y += d * 1.5f;
    float rr = st.FrameRounding;
    if (filled) {
        ImU32 c1 = col::mix(accent, IM_COL32(255, 255, 255, 255), 0.12f * h), c2 = col::mix(accent, orange, 0.55f);
        c2 = col::mix(c2, IM_COL32(255, 255, 255, 255), 0.1f * h);
        glowRect(dl, a, b, rr, accent, 0.5f + h * 1.4f);
        dl->AddRectFilledMultiColor(a, b, fa(c1), fa(c2), fa(c2), fa(c1));
        dl->AddRect(a, b, fa(IM_COL32(255, 255, 255, (int)(60 + 80 * h))), rr, 0, 1.f);
    } else {
        dl->AddRectFilled(a, b, fa(col::mix(IM_COL32(20, 8, 48, 190), accent, 0.10f + 0.22f * h)), rr);
        glowRect(dl, a, b, rr, accent, h);
        dl->AddRect(a, b, fa(withAlpha(accent, 0.45f + 0.5f * h)), rr, 0, 1.3f);
    }
    ImU32 tc = filled ? IM_COL32(20, 4, 40, 255) : col::mix(text, accent, 0.25f * h);
    if (dis) tc = withAlpha(tc, 0.45f);
    float cx = (a.x + b.x) * 0.5f, tw = ts.x + iconW;
    float x0 = cx - tw * 0.5f;
    if ((int)icon >= 0) { drawIcon(dl, icon, ImVec2(x0 + ts.y * 0.5f, (a.y + b.y) * 0.5f), ts.y * 0.95f, fa(tc)); x0 += iconW; }
    ImGui::PushFont(fonts.medium);
    dl->AddText(ImVec2(x0, (a.y + b.y) * 0.5f - ts.y * 0.5f), fa(tc), label, ImGui::FindRenderedTextEnd(label));
    ImGui::PopFont();
    return pressed;
}

bool iconButton(const char* id_, Icon icon, ImU32 accent, float size, const char* tip) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    float sz = size * (ImGui::GetFontSize() / 17.f);
    ImGuiID id = w->GetID(id_);
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x + sz, p.y + sz));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov, held; bool pr = ImGui::ButtonBehavior(bb, id, &hov, &held);
    float h = anim(id, hov ? 1.f : 0.f, 16.f);
    ImDrawList* dl = w->DrawList;
    dl->AddRectFilled(bb.Min, bb.Max, fa(col::mix(IM_COL32(20, 8, 48, 160), accent, 0.1f + 0.3f * h)), 9.f);
    dl->AddRect(bb.Min, bb.Max, fa(withAlpha(accent, 0.35f + 0.6f * h)), 9.f);
    drawIcon(dl, icon, ImVec2((bb.Min.x + bb.Max.x) * .5f, (bb.Min.y + bb.Max.y) * .5f), sz * 0.5f, fa(col::mix(text, accent, 0.6f * h + 0.2f)));
    if (tip && hov) ImGui::SetTooltip("%s", tip);
    return pr;
}

bool navItem(const char* label, Icon icon, bool sel, float width) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    ImGuiID id = w->GetID(label);
    float hgt = ImGui::GetFontSize() * 2.5f;
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x + width, p.y + hgt));
    ImGui::ItemSize(bb, 0); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov, held; bool pr = ImGui::ButtonBehavior(bb, id, &hov, &held);
    float h = anim(id, hov ? 1.f : 0.f, 14.f), s = anim(id + 7, sel ? 1.f : 0.f, 10.f);
    ImDrawList* dl = w->DrawList;
    float slide = 6.f * h * ImGui::GetFontSize() / 17.f;
    if (s > 0.01f || h > 0.01f) {
        ImU32 l = withAlpha(pink, 0.30f * s + 0.10f * h), r = withAlpha(purple, 0.02f * s);
        dl->AddRectFilledMultiColor(bb.Min, bb.Max, fa(l), fa(r), fa(r), fa(l));
    }
    float barH = hgt * (0.35f + 0.5f * s);
    if (s > 0.01f) { dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + (hgt - barH) * .5f), ImVec2(bb.Min.x + 4, bb.Min.y + (hgt + barH) * .5f), fa(withAlpha(pink, s)), 2.f);
                     dl->AddRectFilled(ImVec2(bb.Min.x - 3, bb.Min.y + (hgt - barH) * .5f), ImVec2(bb.Min.x + 8, bb.Min.y + (hgt + barH) * .5f), fa(withAlpha(pink, 0.18f * s)), 4.f); }
    ImU32 tc = col::mix(col::mix(muted, text, h), cyan, s);
    float fs = ImGui::GetFontSize();
    drawIcon(dl, icon, ImVec2(bb.Min.x + fs * 1.6f + slide, bb.Min.y + hgt * .5f), fs * 1.15f, fa(col::mix(tc, pink, s * 0.4f)));
    ImGui::PushFont(s > 0.5f ? fonts.bold : fonts.medium);
    dl->AddText(ImVec2(bb.Min.x + fs * 2.9f + slide, bb.Min.y + (hgt - fs) * .5f), fa(tc), label);
    ImGui::PopFont();
    return pr;
}

bool segmented(const char* id_, const char* const* items, int n, int* sel, float width) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems) return false;
    ImGuiID id = w->GetID(id_);
    float hgt = ImGui::GetFontSize() * 2.1f;
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x + width, p.y + hgt));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    ImDrawList* dl = w->DrawList; float cw = width / n; bool changed = false;
    dl->AddRectFilled(bb.Min, bb.Max, fa(IM_COL32(10, 3, 28, 210)), hgt * .5f);
    dl->AddRect(bb.Min, bb.Max, fa(withAlpha(purple, 0.5f)), hgt * .5f);
    float pos = anim(id, (float)*sel, 14.f);
    ImVec2 a(bb.Min.x + 3 + pos * cw, bb.Min.y + 3), b(bb.Min.x + (pos + 1) * cw - 3, bb.Max.y - 3);
    glowRect(dl, a, b, hgt * .5f, pink, 0.6f);
    dl->AddRectFilledMultiColor(a, b, fa(pink), fa(orange), fa(orange), fa(pink));
    // rounded look: overdraw corners lightly by outline
    for (int i = 0; i < n; ++i) {
        ImRect cell(ImVec2(bb.Min.x + i * cw, bb.Min.y), ImVec2(bb.Min.x + (i + 1) * cw, bb.Max.y));
        ImGuiID cid = id + 100 + i; bool hov, held; ImGui::KeepAliveID(cid);
        if (ImGui::ButtonBehavior(cell, cid, &hov, &held)) { if (*sel != i) { *sel = i; changed = true; } }
        float on = std::max(0.f, 1.f - std::fabs(pos - i));
        ImVec2 ts = ImGui::CalcTextSize(items[i]);
        ImU32 tc = col::mix(hov ? text : muted, IM_COL32(20, 4, 40, 255), on);
        dl->AddText(ImVec2(cell.Min.x + (cw - ts.x) * .5f, cell.Min.y + (hgt - ts.y) * .5f), fa(tc), items[i]);
    }
    return changed;
}

bool chipToggle(const char* label, bool* v, ImU32 accent) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return false;
    ImGuiID id = w->GetID(label); ImVec2 ts = ImGui::CalcTextSize(label);
    float pad = ImGui::GetFontSize() * 0.8f;
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x + ts.x + pad * 2, p.y + ts.y + pad * 0.9f));
    ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, id)) return false;
    bool hov, held; bool pr = ImGui::ButtonBehavior(bb, id, &hov, &held); if (pr) *v = !*v;
    float on = anim(id, *v ? 1.f : 0.f, 14.f), h = anim(id + 1, hov ? 1.f : 0.f, 14.f);
    ImDrawList* dl = w->DrawList; float r = (bb.Max.y - bb.Min.y) * .5f;
    dl->AddRectFilled(bb.Min, bb.Max, fa(col::mix(IM_COL32(20, 8, 48, 180), accent, 0.12f + 0.4f * on + 0.1f * h)), r);
    dl->AddRect(bb.Min, bb.Max, fa(withAlpha(accent, 0.35f + 0.55f * on)), r);
    dl->AddText(ImVec2(bb.Min.x + pad, bb.Min.y + (bb.Max.y - bb.Min.y - ts.y) * .5f), fa(col::mix(muted, text, std::max(on, h))), label);
    return pr;
}

void badge(const char* t, ImU32 c) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return;
    ImGui::PushFont(fonts.small);
    ImVec2 ts = ImGui::CalcTextSize(t), p = w->DC.CursorPos; float px = ts.y * .7f, py = ts.y * .22f;
    ImRect bb(p, ImVec2(p.x + ts.x + px * 2, p.y + ts.y + py * 2));
    ImGui::ItemSize(bb); if (ImGui::ItemAdd(bb, 0)) {
        w->DrawList->AddRectFilled(bb.Min, bb.Max, fa(withAlpha(c, 0.18f)), 20.f);
        w->DrawList->AddRect(bb.Min, bb.Max, fa(withAlpha(c, 0.7f)), 20.f);
        w->DrawList->AddText(ImVec2(bb.Min.x + px, bb.Min.y + py), fa(c), t);
    }
    ImGui::PopFont();
}

void statusDot(ImU32 c, bool pulse) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return;
    float fs = ImGui::GetFontSize(), r = fs * 0.28f; ImVec2 p = w->DC.CursorPos;
    ImRect bb(p, ImVec2(p.x + fs, p.y + fs)); ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, 0)) return;
    ImVec2 ctr(bb.Min.x + fs * .5f, bb.Min.y + fs * .5f);
    if (pulse) { float ph = (float)std::fmod(ImGui::GetTime() * 1.2, 1.0); w->DrawList->AddCircle(ctr, r + ph * fs * 0.45f, fa(withAlpha(c, 0.7f * (1 - ph))), 20, 2.f); }
    w->DrawList->AddCircleFilled(ctr, r * 1.8f, fa(withAlpha(c, 0.15f))); w->DrawList->AddCircleFilled(ctr, r, fa(c));
}

void shimmerBar(float width, float height, float frac_) {
    ImGuiWindow* w = ImGui::GetCurrentWindow(); if (w->SkipItems) return;
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    ImVec2 p = w->DC.CursorPos; ImRect bb(p, ImVec2(p.x + width, p.y + height)); ImGui::ItemSize(bb); if (!ImGui::ItemAdd(bb, 0)) return;
    ImDrawList* dl = w->DrawList; float r = height * .5f;
    dl->AddRectFilled(bb.Min, bb.Max, fa(IM_COL32(10, 3, 28, 220)), r);
    float t = (float)ImGui::GetTime();
    if (frac_ >= 0.f) {
        ImVec2 b(bb.Min.x + width * std::min(1.f, frac_), bb.Max.y);
        dl->AddRectFilledMultiColor(bb.Min, b, fa(pink), fa(orange), fa(orange), fa(pink));
    } else {
        float bw = width * 0.35f, x = bb.Min.x + (width + bw) * frac(t * 0.55f) - bw;
        dl->PushClipRect(bb.Min, bb.Max, true);
        dl->AddRectFilledMultiColor(ImVec2(x, bb.Min.y), ImVec2(x + bw * .5f, bb.Max.y), fa(withAlpha(pink, 0)), fa(pink), fa(pink), fa(withAlpha(pink, 0)));
        dl->AddRectFilledMultiColor(ImVec2(x + bw * .5f, bb.Min.y), ImVec2(x + bw, bb.Max.y), fa(pink), fa(withAlpha(cyan, 0)), fa(withAlpha(cyan, 0)), fa(pink));
        dl->PopClipRect();
    }
    dl->AddRect(bb.Min, bb.Max, fa(withAlpha(purple, 0.5f)), r);
}

bool beginPanel(const char* id, ImVec2 size, const char* title, ImU32 accent, bool scroll) {
    if (!accent) accent = purple;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGuiWindowFlags fl = scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, fl);
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetWindowPos(), s = ImGui::GetWindowSize(), b(a.x + s.x, a.y + s.y);
    float r = ImGui::GetStyle().ChildRounding;
    ImVec2 ia(a.x + 1, a.y + 1), ib(b.x - 1, b.y - 1);
    dl->AddRectFilled(ia, ib, fa(panel), r);
    dl->AddRectFilledMultiColor(ia, ImVec2(ib.x, ia.y + std::min(60.f, s.y)), fa(withAlpha(accent, 0.20f)), fa(withAlpha(accent, 0.20f)), fa(withAlpha(accent, 0.0f)), fa(withAlpha(accent, 0.0f)));
    dl->AddRect(ia, ib, fa(withAlpha(accent, 0.55f)), r, 0, 1.4f);
    if (title) sectionLabel(title, accent);
    return open;
}
void endPanel() { ImGui::EndChild(); }

void sectionLabel(const char* t, ImU32 accent) {
    if (!accent) accent = pink;
    ImGui::PushFont(fonts.bold);
    ImVec2 p = ImGui::GetCursorScreenPos(); float fs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, p.y + fs * 0.12f), ImVec2(p.x + 4, p.y + fs * 1.05f), fa(accent), 2.f);
    dl->AddRectFilled(ImVec2(p.x - 3, p.y + fs * 0.12f), ImVec2(p.x + 7, p.y + fs * 1.05f), fa(withAlpha(accent, 0.2f)), 4.f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 14, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(text));
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor(); ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
}

void gradientText(const char* t, ImU32 a, ImU32 b, ImFont* font) {
    if (font) ImGui::PushFont(font);
    ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
    int n = (int)std::strlen(t); float x = p.x;
    for (int i = 0; i < n; ) {
        int len = 1; unsigned char ch = (unsigned char)t[i]; if (ch >= 0xF0) len = 4; else if (ch >= 0xE0) len = 3; else if (ch >= 0xC0) len = 2;
        ImU32 c = col::mix(a, b, n > 1 ? (float)i / (n - 1) : 0.f);
        dl->AddText(ImVec2(x, p.y), fa(c), t + i, t + i + len);
        x += ImGui::CalcTextSize(t + i, t + i + len).x; i += len;
    }
    ImGui::Dummy(ImVec2(x - p.x, ImGui::GetTextLineHeight()));
    if (font) ImGui::PopFont();
}

void glowText(const char* t, ImU32 c, ImVec2 pos, ImFont* font, float size) {
    ImDrawList* dl = ImGui::GetWindowDrawList(); if (!font) font = ImGui::GetFont(); if (size <= 0) size = font->FontSize;
    for (int i = 3; i >= 1; --i) for (int k = 0; k < 4; ++k) {
        float dx = (k == 0 ? 1 : k == 1 ? -1 : 0) * i * 1.3f, dy = (k == 2 ? 1 : k == 3 ? -1 : 0) * i * 1.3f;
        dl->AddText(font, size, ImVec2(pos.x + dx, pos.y + dy), fa(withAlpha(c, 0.10f)), t);
    }
    dl->AddText(font, size, pos, fa(c), t);
}

void helpMarker(const char* t) {
    ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(muted), "(?)");
    if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26); ImGui::TextUnformatted(t); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
}

bool inputText(const char* label, std::string* s, const char* hint, int flags) {
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (hint) return ImGui::InputTextWithHint(label, hint, s, flags);
    return ImGui::InputText(label, s, flags);
}
bool inputMultiline(const char* label, std::string* s, ImVec2 size, int flags) { return ImGui::InputTextMultiline(label, s, size, flags); }

// ---------------------------------------------------------------- toasts
struct ToastItem { Toast kind; std::string msg; double t0; float dur; };
static std::vector<ToastItem> g_toasts;
void toast(Toast k, const std::string& m, float d) { g_toasts.push_back({k, m, ImGui::GetTime(), d}); if (g_toasts.size() > 6) g_toasts.erase(g_toasts.begin()); }
void drawToasts(ImVec2 vp) {
    ImDrawList* dl = ImGui::GetForegroundDrawList(); double now = ImGui::GetTime(); float fs = ImGui::GetFontSize();
    float y = fs * 3.4f, wmax = fs * 22;
    for (size_t i = 0; i < g_toasts.size();) {
        ToastItem& t = g_toasts[i]; float age = (float)(now - t.t0);
        if (age > t.dur + 0.4f) { g_toasts.erase(g_toasts.begin() + i); continue; }
        float in = easeOut(age / 0.35f), out = age > t.dur ? easeOut((age - t.dur) / 0.4f) : 0.f;
        ImU32 c = t.kind == Toast::Success ? green : t.kind == Toast::Warn ? yellow : t.kind == Toast::Error ? red : cyan;
        ImVec2 ts = ImGui::CalcTextSize(t.msg.c_str(), nullptr, false, wmax - fs * 2.5f);
        float w = ts.x + fs * 2.6f, h = ts.y + fs * 1.2f;
        float x = vp.x - (w + fs) * in + (w + fs) * out;
        float a = (1.f - out);
        ImVec2 p0(x, y), p1(x + w, y + h);
        dl->AddRectFilled(p0, p1, withAlpha(IM_COL32(20, 6, 48, 235), a), 12.f);
        for (int g = 3; g >= 1; --g) dl->AddRect(ImVec2(p0.x - g * 2, p0.y - g * 2), ImVec2(p1.x + g * 2, p1.y + g * 2), withAlpha(c, 0.07f * a * (4 - g)), 12.f + g * 2, 0, 2.f);
        dl->AddRect(p0, p1, withAlpha(c, 0.9f * a), 12.f, 0, 1.4f);
        dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 8), ImVec2(p0.x + 5, p1.y - 8), withAlpha(c, a), 2.f);
        dl->AddText(nullptr, 0.f, ImVec2(p0.x + fs * 1.1f, p0.y + fs * 0.6f), withAlpha(text, a), t.msg.c_str(), nullptr, wmax - fs * 2.5f);
        y += h + fs * 0.6f; ++i;
    }
}

float pageEnter(int pageId) {
    static int cur = -1; static double t0 = 0;
    if (cur != pageId) { cur = pageId; t0 = ImGui::GetTime(); }
    if (!g_anim) return 1.f;
    return easeOut((float)(ImGui::GetTime() - t0) / 0.38f);
}

}} // namespace
