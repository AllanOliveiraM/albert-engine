add_library(albert_core STATIC scene.cpp layers.cpp model.cpp bodies.cpp local_system.cpp astro_time.cpp bridge.cpp time.cpp time_service.cpp simulation.cpp approximate_ephemeris.cpp comets.cpp)
target_include_directories(albert_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(albert_core PUBLIC Threads::Threads PRIVATE CURL::libcurl albert_erfa nlohmann_json::nlohmann_json)

if(WIN32)
    target_link_libraries(albert_core PRIVATE ws2_32)
endif()
