cmake_minimum_required(VERSION 3.20)
project(ScanDisplayClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ScanDisplay WIN32
    src/main.cpp
)

target_compile_definitions(ScanDisplay PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(ScanDisplay PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(ScanDisplay PRIVATE
    winhttp
    shell32
    comctl32
    ole32
)

install(TARGETS ScanDisplay RUNTIME DESTINATION .)
