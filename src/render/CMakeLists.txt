add_library(albert_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui-wgpu/imgui_impl_wgpu.cpp
)
target_compile_definitions(albert_imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
target_include_directories(albert_imgui SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/imgui-wgpu ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(albert_imgui PUBLIC glfw albert_wgpu ${CMAKE_DL_LIBS})

add_library(albert_render STATIC application.cpp display.cpp window_icon.cpp lighting.cpp layers.cpp solids.cpp ring_systems.cpp comets.cpp shapes.cpp objects.cpp rocks.cpp eclipses.cpp debris.cpp visuals.cpp uploads.cpp paths.cpp borders.cpp clouds.cpp asset_pack.cpp asset_downloads.cpp cache.cpp regional.cpp wms.cpp)
target_include_directories(albert_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${threepp_SOURCE_DIR}/src/external/stb ${threepp_SOURCE_DIR}/src/external/glad)
target_link_libraries(albert_render PRIVATE threepp::threepp albert_gpu
    nlohmann_json::nlohmann_json albert_imgui albert_download
    Threads::Threads albert_erfa albert_providers albert_catalog)
target_link_libraries(albert_render PUBLIC albert_core albert_download)
set(ALBERT_ASSET_PACK "${PROJECT_BINARY_DIR}/imagery/visuals.pak" CACHE FILEPATH "Assembled local imagery library")
target_compile_definitions(albert_render PRIVATE ALBERT_DEVELOPMENT_PACK_PATH="${ALBERT_ASSET_PACK}")
set(ALBERT_ASSET_BASE_URL "https://github.com/AllanOliveiraM/albert-engine/releases/download" CACHE STRING "Release download base for visual asset containers")
target_compile_definitions(albert_render PRIVATE ALBERT_ASSET_BASE_URL="${ALBERT_ASSET_BASE_URL}")
if(MSVC)
    target_compile_options(albert_render PRIVATE /W4 /utf-8 /permissive-)
    target_compile_definitions(albert_render PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(albert_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ALBERT_RPC)
    target_link_libraries(albert_render PRIVATE albert_rpc)
    target_compile_definitions(albert_render PRIVATE ALBERT_HAS_RPC=1)
endif()
