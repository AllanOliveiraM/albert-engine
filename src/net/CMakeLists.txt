add_library(albert_download STATIC download.cpp)
target_link_libraries(albert_download PUBLIC CURL::libcurl)
target_include_directories(albert_download PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
