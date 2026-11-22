// Headless screenshot tool: runs the real UI code and software-rasterizes ImGui draw data to a PNG.
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/app.h"
#include "ui/theme.h"
#include "app/fsutil.h"
#include "stb_image_write.h"

static unsigned char* g_atlas = nullptr; static int g_aw = 0, g_ah = 0;
struct Px { float r, g, b, a; };

static void rasterTri(std::vector<uint8_t>& fb, int W, int H, const ImDrawVert& a0, const ImDrawVert& b0, const ImDrawVert& c0, const ImVec4& clip) {
    ImDrawVert a = a0, b = b0, c = c0;
    float dd = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
    if (dd < 0) std::swap(b, c);   // make every triangle the same orientation so the top-left rule is consistent
    float minx = std::min({a.pos.x, b.pos.x, c.pos.x}), maxx = std::max({a.pos.x, b.pos.x, c.pos.x});
    float miny = std::min({a.pos.y, b.pos.y, c.pos.y}), maxy = std::max({a.pos.y, b.pos.y, c.pos.y});
    int x0 = std::max((int)std::floor(minx), (int)clip.x), x1 = std::min((int)std::ceil(maxx), (int)clip.z - 1);
    int y0 = std::max((int)std::floor(miny), (int)clip.y), y1 = std::min((int)std::ceil(maxy), (int)clip.w - 1);
    x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, W - 1); y1 = std::min(y1, H - 1);
    float d = (b.pos.y - c.pos.y) * (a.pos.x - c.pos.x) + (c.pos.x - b.pos.x) * (a.pos.y - c.pos.y);
    if (std::fabs(d) < 1e-6f) return;
    ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a.col), cb = ImGui::ColorConvertU32ToFloat4(b.col), cc = ImGui::ColorConvertU32ToFloat4(c.col);
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        float px = x + 0.5f, py = y + 0.5f;
        float w0 = ((b.pos.y - c.pos.y) * (px - c.pos.x) + (c.pos.x - b.pos.x) * (py - c.pos.y)) / d;
        float w1 = ((c.pos.y - a.pos.y) * (px - c.pos.x) + (a.pos.x - c.pos.x) * (py - c.pos.y)) / d;
        float w2 = 1.f - w0 - w1;
        // top-left fill rule: pixels exactly on a shared edge are drawn by exactly one triangle
        auto inside = [&](float w, const ImVec2& p0, const ImVec2& p1) { if (w > 1e-6f) return true; if (w < -1e-6f) return false; float dx = p1.x - p0.x, dy = p1.y - p0.y; return dy < 0 || (dy == 0 && dx > 0); };
        if (!inside(w0, b.pos, c.pos) || !inside(w1, c.pos, a.pos) || !inside(w2, a.pos, b.pos)) continue;
        float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x, v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
        int tx = std::min(g_aw - 1, std::max(0, (int)(u * g_aw))), ty = std::min(g_ah - 1, std::max(0, (int)(v * g_ah)));
        float ta = g_atlas[(ty * g_aw + tx) * 4 + 3] / 255.f;
        float r = w0 * ca.x + w1 * cb.x + w2 * cc.x, g = w0 * ca.y + w1 * cb.y + w2 * cc.y, bl = w0 * ca.z + w1 * cb.z + w2 * cc.z, al = (w0 * ca.w + w1 * cb.w + w2 * cc.w) * ta;
        uint8_t* p = &fb[((size_t)y * W + x) * 4];
        p[0] = (uint8_t)(p[0] * (1 - al) + r * 255 * al); p[1] = (uint8_t)(p[1] * (1 - al) + g * 255 * al); p[2] = (uint8_t)(p[2] * (1 - al) + bl * 255 * al); p[3] = 255;
    }
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "/tmp/claude-0/shot.png";
    std::string page = argc > 2 ? argv[2] : "workspace";
    int W = argc > 3 ? atoi(argv[3]) : 1280, H = argc > 4 ? atoi(argv[4]) : 800;
    int settingsTab = argc > 5 ? atoi(argv[5]) : -1;
    float scale = 1.f;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    he::ui::loadFonts(scale); he::ui::applyStyle(scale);
    int aw, ah; unsigned char* px; io.Fonts->GetTexDataAsRGBA32(&px, &aw, &ah); g_atlas = px; g_aw = aw; g_ah = ah;
    he::App app; app.init(); app.setDpiScale(1.f);
    app.debugLoadDemo();
    if (page == "plan") app.debugSetPage(he::Page::Plan); else if (page == "memory") app.debugSetPage(he::Page::Memory);
    else if (page == "output") app.debugSetPage(he::Page::Output); else if (page == "about") app.debugSetPage(he::Page::About);
    if (settingsTab >= 0) app.debugOpenSettings(settingsTab);
    std::vector<uint8_t> fb;
    for (int f = 0; f < 90; ++f) {
        io.DisplaySize = ImVec2((float)W, (float)H); io.DeltaTime = 1.f / 30.f;
        if (getenv("MX")) { float mx=atof(getenv("MX")), my=atof(getenv("MY"));
            if (f==50) io.AddMousePosEvent(mx,my); if (f==55) io.AddMouseButtonEvent(0,true); if (f==58) io.AddMouseButtonEvent(0,false); }
        ImGui::NewFrame(); app.frame(io.DisplaySize); if (getenv("MX") && f>=54 && f<=60) { ImGuiContext& g=*ImGui::GetCurrentContext(); std::printf("f%d hov=%u act=%u hovwin=%s navwin=%s down=%d\n", f, g.HoveredId, g.ActiveId, g.HoveredWindow?g.HoveredWindow->Name:"-", g.NavWindow?g.NavWindow->Name:"-", (int)io.MouseDown[0]); } ImGui::Render();
        if (f < 89) continue;
        fb.assign((size_t)W * H * 4, 0);
        ImDrawData* dd = ImGui::GetDrawData();
        for (int n = 0; n < dd->CmdListsCount; ++n) {
            const ImDrawList* cl = dd->CmdLists[n];
            for (int c = 0; c < cl->CmdBuffer.Size; ++c) {
                const ImDrawCmd& cmd = cl->CmdBuffer[c];
                if (cmd.UserCallback) continue;
                for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                    const ImDrawVert* vb = cl->VtxBuffer.Data + cmd.VtxOffset;
                    ImDrawIdx i0 = cl->IdxBuffer[cmd.IdxOffset + i], i1 = cl->IdxBuffer[cmd.IdxOffset + i + 1], i2 = cl->IdxBuffer[cmd.IdxOffset + i + 2];
                    rasterTri(fb, W, H, vb[i0], vb[i1], vb[i2], cmd.ClipRect);
                }
            }
        }
    }
    stbi_write_png(out.c_str(), W, H, 4, fb.data(), W * 4);
    app.shutdown();
    std::printf("wrote %s\n", out.c_str());
    return 0;
}
