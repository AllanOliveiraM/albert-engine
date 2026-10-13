add_library(albert_providers STATIC horizons.cpp small_bodies.cpp)
target_include_directories(albert_providers PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_providers PUBLIC albert_core albert_download nlohmann_json::nlohmann_json)
