add_library(albert_core STATIC scene.cpp bridge.cpp)
target_include_directories(albert_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_core PUBLIC Threads::Threads)
