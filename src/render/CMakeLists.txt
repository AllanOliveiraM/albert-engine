add_library(albert_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${PROJECT_SOURCE_DIR}/third_party/imgui-wgpu/imgui_impl_wgpu.cpp
)
target_compile_definitions(albert_imgui PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_WGPU)
target_include_directories(albert_imgui SYSTEM PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/imgui-wgpu ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(albert_imgui PUBLIC glfw albert_wgpu ${CMAKE_DL_LIBS})

# miniaudio 0.11.25 (Unlicense or MIT No Attribution): the header and its licence from the release tag, pinned by SHA-256.
set(albert_miniaudio_dir "${PROJECT_BINARY_DIR}/_deps/miniaudio")
foreach(item IN ITEMS "miniaudio.h;ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89" "LICENSE;457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb")
    list(GET item 0 name)
    list(GET item 1 hash)
    set(have "")
    if(EXISTS "${albert_miniaudio_dir}/${name}")
        file(SHA256 "${albert_miniaudio_dir}/${name}" have)
    endif()
    if(NOT have STREQUAL hash)
        file(DOWNLOAD "https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/${name}" "${albert_miniaudio_dir}/${name}" EXPECTED_HASH SHA256=${hash} TLS_VERIFY ON)
    endif()
endforeach()
add_library(albert_miniaudio STATIC miniaudio.c)
target_include_directories(albert_miniaudio SYSTEM PUBLIC ${albert_miniaudio_dir})
target_link_libraries(albert_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(albert_miniaudio PUBLIC m)
endif()
if(MSVC)
    target_compile_options(albert_miniaudio PRIVATE /w)
else()
    target_compile_options(albert_miniaudio PRIVATE -w)
endif()

add_library(albert_render STATIC application.cpp broadcast_camera.cpp display.cpp sound.cpp webgpu_programs.cpp window_icon.cpp lighting.cpp site_lighting.cpp layers.cpp solids.cpp ring_systems.cpp comets.cpp shapes.cpp objects.cpp vehicle.cpp scenery.cpp launch_cloud.cpp splashdown.cpp contact_shading.cpp industrial.cpp production.cpp roads.cpp broadcast.cpp rocks.cpp eclipses.cpp debris.cpp visuals.cpp uploads.cpp paths.cpp borders.cpp clouds.cpp asset_pack.cpp asset_downloads.cpp cache.cpp regional.cpp wms.cpp terrain.cpp terrain_heights.cpp)
target_include_directories(albert_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${threepp_SOURCE_DIR}/src/external/stb)
target_link_libraries(albert_render PRIVATE threepp::threepp albert_gpu
    nlohmann_json::nlohmann_json albert_imgui albert_download
    Threads::Threads albert_erfa albert_providers albert_catalog albert_miniaudio)
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
    target_sources(albert_render PRIVATE playground.cpp) # The interference panel drives flights through the API.
    target_link_libraries(albert_render PRIVATE albert_rpc)
    target_compile_definitions(albert_render PRIVATE ALBERT_HAS_RPC=1)
endif()
