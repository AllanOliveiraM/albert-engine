include(FetchContent)

# Archives and SHA-256 checksums deliberately pin every downloaded library.
# GLFW 3.4, GLAD and the other threepp dependencies are in its pinned archive.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
foreach(feature IN ITEMS EXAMPLES TESTS EDITOR)
    set(THREEPP_BUILD_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(feature IN ITEMS AUDIO FBX USD RLTOOLS PYTHON VULKAN)
    set(THREEPP_WITH_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(THREEPP_WITH_GLFW ON CACHE BOOL "" FORCE)
set(THREEPP_USE_EXTERNAL_GLFW OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(threepp
    URL https://codeload.github.com/markaren/threepp/tar.gz/8518a6c22d051775c45dbd0c04a3ba92e92a5366
    URL_HASH SHA256=5843fbccd7c3fec59dd39ab3a4e541d5d677f2e1372b883c03f4271805252b02
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/6ded5230d043aa32c755e65c910c2af5002fb9f9
    URL_HASH SHA256=61ae06b25571161cb69ec1401f830cdb02f6bd685c39840daf087991fd7d8851
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(erfa
    URL https://codeload.github.com/liberfa/erfa/tar.gz/refs/tags/v2.0.1
    URL_HASH SHA256=d5469fbd0b212b3c7270c1da15c9bd82f37da9218fc89627f98283d27b416cbf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(threepp json imgui erfa)
set_property(TARGET threepp PROPERTY SYSTEM TRUE)
set_property(TARGET nlohmann_json PROPERTY SYSTEM TRUE)

# ERFA ships autotools sources. Compile the pinned C library directly on all
# platforms, excluding the upstream test executables from the library.
file(GLOB erfa_sources "${erfa_SOURCE_DIR}/src/*.c")
list(FILTER erfa_sources EXCLUDE REGEX "/t_erfa_c(_extra)?\\.c$")
add_library(albert_erfa STATIC ${erfa_sources})
target_include_directories(albert_erfa SYSTEM PUBLIC ${erfa_SOURCE_DIR}/src)
target_compile_definitions(albert_erfa PRIVATE PACKAGE_VERSION="2.0.1"
    PACKAGE_VERSION_MAJOR=2 PACKAGE_VERSION_MINOR=0 PACKAGE_VERSION_MICRO=1
    SOFA_VERSION="20231011")
if(UNIX)
    target_link_libraries(albert_erfa PUBLIC m)
endif()

# Scope curl's generic options so they do not turn off Albert's tests.
function(albert_fetch_curl)
    set(BUILD_TESTING OFF)
    set(BUILD_EXAMPLES OFF)
    set(BUILD_CURL_EXE OFF)
    set(BUILD_STATIC_LIBS ON)
    set(BUILD_LIBCURL_DOCS OFF)
    set(BUILD_MISC_DOCS OFF)
    set(ENABLE_CURL_MANUAL OFF)
    set(CURL_DISABLE_INSTALL ON)
    set(CURL_STATIC_CRT ON)
    set(HTTP_ONLY ON)
    # Time synchronization reuses curl's bounded asynchronous DNS resolver.
    set(ENABLE_THREADED_RESOLVER ON)
    # curl declares these as CACHE STRING under older CMake policies.
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE STRING "" FORCE)
    set(CURL_ZSTD OFF CACHE STRING "" FORCE)
    set(USE_NGHTTP2 OFF)
    set(USE_LIBIDN2 OFF)
    set(CURL_USE_LIBPSL OFF)
    set(CURL_USE_LIBSSH2 OFF)
    set(CURL_USE_SCHANNEL ${WIN32})
    if(WIN32)
        set(CURL_USE_OPENSSL OFF)
    else()
        set(CURL_USE_OPENSSL ON)
        find_package(OpenSSL REQUIRED)
    endif()
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.22.0.tar.xz
        URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(curl)
endfunction()
albert_fetch_curl()
FetchContent_GetProperties(curl)
