cmake_minimum_required(VERSION 3.25...4.2)
project(AlbertHostTools LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
include(../../cmake/Grpc.cmake)
add_custom_target(albert_host_tools
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:protoc> ${CMAKE_BINARY_DIR}/bin/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:grpc_cpp_plugin> ${CMAKE_BINARY_DIR}/bin/
    DEPENDS protoc grpc_cpp_plugin)
