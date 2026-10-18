add_library(albert_providers STATIC horizons.cpp small_bodies.cpp store.cpp gfs.cpp space_weather.cpp)
target_include_directories(albert_providers PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_providers PUBLIC albert_core albert_download albert_physics nlohmann_json::nlohmann_json)
