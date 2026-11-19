cmake_minimum_required(VERSION 3.16)
project(HappyEngineering VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(HE_BUILD_GUI   "Build the GUI application" ON)
option(HE_BUILD_TESTS "Build the core unit tests" ON)
option(HE_STATIC      "Link runtime libraries statically" ON)

# ---------------------------------------------------------------- tests
if(HE_BUILD_TESTS)
  enable_testing()
  foreach(t test_core test_edit)
    add_executable(${t} test/${t}.cpp)
    target_include_directories(${t} PRIVATE src)
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()

if(NOT HE_BUILD_GUI)
  return()
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9 GIT_SHALLOW TRUE)
FetchContent_Declare(glfw  GIT_REPOSITORY https://github.com/glfw/glfw.git     GIT_TAG 3.4    GIT_SHALLOW TRUE)
FetchContent_Declare(stb   GIT_REPOSITORY https://github.com/nothings/stb.git  GIT_TAG 9600522de53973336fa033a289858a4354cbb04c)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)
endif()
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC glfw)

# embedded binary assets
set(EMBED_DIR ${CMAKE_BINARY_DIR}/embedded)
file(MAKE_DIRECTORY ${EMBED_DIR})
function(he_embed file sym)
  set(out ${EMBED_DIR}/${sym}.cpp)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_SOURCE_DIR}/${file} -DOUT=${out} -DSYM=${sym} -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${CMAKE_SOURCE_DIR}/${file} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake VERBATIM)
  set(HE_EMBEDDED ${HE_EMBEDDED} ${out} PARENT_SCOPE)
endfunction()
he_embed(assets/icon.png icon_png)
he_embed(assets/fonts/Poppins-Regular.ttf font_regular)
he_embed(assets/fonts/Poppins-Medium.ttf font_medium)
he_embed(assets/fonts/Poppins-Bold.ttf font_bold)
he_embed(assets/fonts/DejaVuSansMono.ttf font_mono)

set(HE_SOURCES src/main.cpp)
if(WIN32)
  list(APPEND HE_SOURCES src/app.rc)
endif()
add_executable(happy-engineering ${HE_SOURCES} ${HE_EMBEDDED})
target_include_directories(happy-engineering PRIVATE src ${stb_SOURCE_DIR})
target_link_libraries(happy-engineering PRIVATE imgui)
set_target_properties(happy-engineering PROPERTIES OUTPUT_NAME HappyEngineering)

if(WIN32)
  target_link_libraries(happy-engineering PRIVATE winhttp comdlg32 gdi32 shell32 ole32 opengl32 crypt32)
  target_compile_definitions(happy-engineering PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0601 NOMINMAX)
  if(MINGW)
    target_link_options(happy-engineering PRIVATE -mwindows)
  endif()
elseif(UNIX)
  find_package(Threads REQUIRED)
  target_link_libraries(happy-engineering PRIVATE Threads::Threads dl)
endif()
if(HE_STATIC AND MINGW)
  target_link_options(happy-engineering PRIVATE -static -static-libgcc -static-libstdc++)
endif()
install(TARGETS happy-engineering RUNTIME DESTINATION .)
