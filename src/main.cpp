// Happy Engineering - minimal bootstrap (S1): window + ImGui + embedded font/icon.
#include <cstdio>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern const unsigned char icon_png[]; extern const std::size_t icon_png_size;
extern const unsigned char font_regular[]; extern const std::size_t font_regular_size;

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* w = glfwCreateWindow(1280, 800, "Happy Engineering", nullptr, nullptr);
    if (!w) { glfwTerminate(); return 2; }
    int iw, ih, ic;
    unsigned char* px = stbi_load_from_memory(icon_png, (int)icon_png_size, &iw, &ih, &ic, 4);
    if (px) { GLFWimage im{iw, ih, px}; glfwSetWindowIcon(w, 1, &im); stbi_image_free(px); }
    glfwMakeContextCurrent(w);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImFontConfig cfg; cfg.FontDataOwnedByAtlas = false;
    ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)font_regular, (int)font_regular_size, 18.f, &cfg);
    ImGui_ImplGlfw_InitForOpenGL(w, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    while (!glfwWindowShouldClose(w)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        ImGui::Begin("Happy Engineering"); ImGui::Text(":)"); ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(w);
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(w); glfwTerminate();
    return 0;
}
