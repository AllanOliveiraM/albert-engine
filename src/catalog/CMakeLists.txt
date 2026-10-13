add_library(albert_catalog STATIC catalog.cpp)
target_include_directories(albert_catalog PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_catalog PUBLIC albert_providers albert_core)
