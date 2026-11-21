// Happy Engineering - application entry point (GLFW + OpenGL + Dear ImGui).
#include <cstdio>
#include <vector>
#include <string>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"   // GL function pointers (already loaded by the backend)
#include <GLFW/glfw3.h>
#include "ui/app.h"
#include "ui/theme.h"
#include "app/imgconv.h"

extern const unsigned char icon_png[]; extern const std::size_t icon_png_size;

static he::App* g_app = nullptr;
static void onDrop(GLFWwindow*, int count, const char** paths) {
    if (!g_app) return;
    std::vector<std::string> v;
    for (int i = 0; i < count; ++i) v.push_back(paths[i]);   // GLFW delivers UTF-8
    g_app->onDrop(v);
}
static void onGlfwError(int code, const char* msg) { std::fprintf(stderr, "GLFW error %d: %s\n", code, msg); }

static GLFWwindow* makeWindow(const char** glslOut) {
    // 1) modern core profile  2) compatibility fallback for old GPUs / drivers
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* w = glfwCreateWindow(1360, 860, "Happy Engineering", nullptr, nullptr);
    if (w) { *glslOut = "#version 150"; return w; }
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    w = glfwCreateWindow(1360, 860, "Happy Engineering", nullptr, nullptr);
    if (w) { *glslOut = "#version 130"; return w; }
    return nullptr;
}

int main() {
    glfwSetErrorCallback(onGlfwError);
    if (!glfwInit()) return 1;
    const char* glsl = "#version 150";
    GLFWwindow* win = makeWindow(&glsl);
    if (!win) {
        std::fprintf(stderr, "Could not create an OpenGL window. Update your graphics driver.\n");
        glfwTerminate(); return 2;
    }
    glfwSetWindowSizeLimits(win, 900, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
    {   // window icon from the embedded PNG
        he::Image img; std::string err;
        std::vector<uint8_t> bytes(icon_png, icon_png + icon_png_size);
        if (he::decodeImage(bytes, img, err)) { GLFWimage gi; gi.width = img.w; gi.height = img.h; gi.pixels = img.rgba.data(); glfwSetWindowIcon(win, 1, &gi); }
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl);

    he::App app; g_app = &app;
    app.init();
    glfwSetDropCallback(win, onDrop);

    while (!glfwWindowShouldClose(win) && !app.wantsQuit()) {
        if (glfwGetWindowAttrib(win, GLFW_ICONIFIED)) { glfwWaitEventsTimeout(0.2); continue; }
        glfwPollEvents();
        float xs = 1.f, ys = 1.f; glfwGetWindowContentScale(win, &xs, &ys);
        app.setDpiScale(xs > 0.5f ? xs : 1.f);
        float scale;
        if (app.consumeScaleChange(scale)) {
            he::ui::loadFonts(scale); he::ui::applyStyle(scale);
            ImGui_ImplOpenGL3_DestroyDeviceObjects();   // re-created (with the new font atlas) in NewFrame
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        app.frame(io.DisplaySize);
        ImGui::Render();
        int fw, fh; glfwGetFramebufferSize(win, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0.04f, 0.01f, 0.11f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }
    app.shutdown();
    g_app = nullptr;
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}
