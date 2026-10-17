include(FetchContent)

# gRPC 1.71.0 and exactly its upstream dependency revisions.
function(albert_fetch_grpc)
    set(BUILD_TESTING OFF)
    set(RE2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    FetchContent_Declare(albert_absl
        URL https://codeload.github.com/abseil/abseil-cpp/tar.gz/4447c7562e3bc702ade25105912dce503f0c4010
        URL_HASH SHA256=df8b3e0da03567badd9440377810c39a38ab3346fa89df077bb52e68e4d61e74
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_absl)
    if(WIN32)
    FetchContent_Declare(albert_boringssl
        URL https://codeload.github.com/google/boringssl/tar.gz/c64b8fefbba9a9dadda73138062fc449bdf11e2a
        URL_HASH SHA256=cea4c77cd69279585ea53efa2bb2ae6eb5f31fb39c214213c5b7cdf3c44d7b52
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_boringssl)
    endif()
    FetchContent_Declare(albert_cares
        URL https://codeload.github.com/c-ares/c-ares/tar.gz/6360e96b5cf8e5980c887ce58ef727e53d77243a
        URL_HASH SHA256=bf26e5b25e259911914a85ae847b6d723488adb5af4f8bdeb9d0871a318476e3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_cares)
    FetchContent_Declare(albert_protobuf
        URL https://codeload.github.com/protocolbuffers/protobuf/tar.gz/2d4414f384dc499af113b5991ce3eaa9df6dd931
        URL_HASH SHA256=cf2db029202bb8eb1471b9bae387cc475d15d9e99c547e6906155033f81249a5
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_protobuf)
    FetchContent_Declare(albert_re2
        URL https://codeload.github.com/google/re2/tar.gz/0c5616df9c0aaa44c9440d87422012423d91c7d1
        URL_HASH SHA256=05a1960729d1053f170ba1ac52bd442cd47cdfbb95348f935fa1921439a9b80c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_re2)
    FetchContent_Declare(albert_zlib
        URL https://codeload.github.com/madler/zlib/tar.gz/09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851
        URL_HASH SHA256=18337cdb32562003c39d9f7322b9a166ad4abfb2b909566428e11f96d2385586
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_zlib)
    set(ABSL_ROOT_DIR "${albert_absl_SOURCE_DIR}")
    set(BORINGSSL_ROOT_DIR "${albert_boringssl_SOURCE_DIR}")
    set(CARES_ROOT_DIR "${albert_cares_SOURCE_DIR}")
    set(PROTOBUF_ROOT_DIR "${albert_protobuf_SOURCE_DIR}")
    set(RE2_ROOT_DIR "${albert_re2_SOURCE_DIR}")
    set(ZLIB_ROOT_DIR "${albert_zlib_SOURCE_DIR}")
    set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
    if(CMAKE_CROSSCOMPILING)
        set(gRPC_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
        set(gRPC_BUILD_GRPC_CPP_PLUGIN OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
        set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
    else()
        set(gRPC_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
        set(gRPC_BUILD_GRPC_CPP_PLUGIN ON CACHE BOOL "" FORCE)
        set(protobuf_BUILD_PROTOC_BINARIES ON CACHE BOOL "" FORCE)
    endif()
    foreach(language CSHARP NODE OBJECTIVE_C PHP PYTHON RUBY)
        set(gRPC_BUILD_GRPC_${language}_PLUGIN OFF CACHE BOOL "" FORCE)
    endforeach()
    set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
    set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # The whole program links the static MSVC runtime (/MT). Abseil otherwise
    # forces the DLL runtime in its own directory, so protoc failed with LNK2038.
    set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(CARES_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    set(gRPC_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
    if(WIN32)
        set(gRPC_SSL_PROVIDER module CACHE STRING "" FORCE)
        set(OPENSSL_NO_ASM ON CACHE BOOL "" FORCE)
    else()
        set(gRPC_SSL_PROVIDER package CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(albert_grpc
        URL https://codeload.github.com/grpc/grpc/tar.gz/4e9357bca1408596663a218c0c608a4c0560a867
        URL_HASH SHA256=f82a9a85bb28ebb552c3418efcdb2cc5e85850e3e8c4883303debe08348d3d55
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR albert-fetch-only)
    FetchContent_MakeAvailable(albert_grpc)
    add_subdirectory(${albert_grpc_SOURCE_DIR} ${albert_grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
endfunction()
albert_fetch_grpc()
