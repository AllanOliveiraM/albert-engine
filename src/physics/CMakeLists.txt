add_library(albert_physics STATIC earth.cpp atmosphere.cpp flight.cpp vehicles.cpp weather.cpp)
target_include_directories(albert_physics PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_physics PRIVATE albert_erfa)
