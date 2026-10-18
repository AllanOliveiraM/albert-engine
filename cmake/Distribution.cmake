install(TARGETS render RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES
    ${PROJECT_SOURCE_DIR}/assets/countries.geojson
    ${PROJECT_SOURCE_DIR}/assets/stars.csv
    ${PROJECT_SOURCE_DIR}/assets/cloud-colormap.json
    DESTINATION assets COMPONENT Runtime)
install(FILES ${PROJECT_SOURCE_DIR}/assets/fonts/Inter.ttf
    DESTINATION assets/fonts COMPONENT Runtime)
install(FILES ${PROJECT_SOURCE_DIR}/assets/branding/albert.png
    DESTINATION assets/branding COMPONENT Runtime)
install(FILES ${PROJECT_SOURCE_DIR}/assets/fonts/OFL.txt
    DESTINATION licenses/Inter COMPONENT Runtime)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/orekit/LICENSE.txt ${PROJECT_SOURCE_DIR}/third_party/orekit/NOTICE.txt
    DESTINATION licenses/Orekit COMPONENT Runtime)
install(FILES ${PROJECT_SOURCE_DIR}/third_party/wgpu-native/LICENSE.MIT ${PROJECT_SOURCE_DIR}/third_party/wgpu-native/LICENSE.APACHE
    DESTINATION licenses/wgpu-native COMPONENT Runtime)
install(CODE "if(NOT EXISTS \"${ALBERT_ASSET_PACK}\")\n  message(FATAL_ERROR \"Complete visuals.pak missing. Run make assets before creating a distribution.\")\nendif()" COMPONENT Runtime)
install(FILES "${ALBERT_ASSET_PACK}" DESTINATION assets RENAME visuals.pak COMPONENT Runtime)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Bundle OpenSSL, libstdc++ and other redistributable runtime libraries.
    # glibc and the graphics driver must come from the host OS.
    configure_file(${PROJECT_SOURCE_DIR}/cmake/BundleLinux.cmake.in BundleLinux.cmake @ONLY)
    install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/BundleLinux.cmake COMPONENT Runtime)
endif()
install(FILES ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md DESTINATION licenses COMPONENT Runtime)
set(license_dependencies threepp imgui json erfa curl)
if(ALBERT_RPC)
    foreach(name grpc absl protobuf re2 cares zlib boringssl)
        FetchContent_GetProperties(albert_${name})
        if(albert_${name}_SOURCE_DIR)
            list(APPEND license_dependencies albert_${name})
        endif()
    endforeach()
endif()
foreach(dep IN LISTS license_dependencies)
    file(GLOB_RECURSE licenses LIST_DIRECTORIES FALSE
        "${${dep}_SOURCE_DIR}/*LICENSE*" "${${dep}_SOURCE_DIR}/*COPYING*"
        "${${dep}_SOURCE_DIR}/*COPYRIGHT*" "${${dep}_SOURCE_DIR}/*license*")
    foreach(license IN LISTS licenses)
        file(RELATIVE_PATH relative "${${dep}_SOURCE_DIR}" "${license}")
        get_filename_component(directory "${relative}" DIRECTORY)
        install(FILES "${license}"
            DESTINATION licenses/${dep}/${directory}
            COMPONENT Runtime)
    endforeach()
endforeach()
