add_library(albert_gpu STATIC context.cpp window.cpp programs.cpp scene.cpp atmosphere.cpp ocean.cpp clouds.cpp)
target_include_directories(albert_gpu PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_gpu PUBLIC albert_wgpu threepp::threepp PRIVATE glfw)
