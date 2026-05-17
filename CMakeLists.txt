cmake_minimum_required(VERSION 3.21)
project(TraySampleApp LANGUAGES CXX)

add_executable(TraySampleApp WIN32 main.cpp)

target_compile_features(TraySampleApp PRIVATE cxx_std_17)
target_compile_definitions(TraySampleApp PRIVATE UNICODE _UNICODE)
target_link_libraries(TraySampleApp PRIVATE shell32 advapi32)

if(MSVC)
    target_compile_options(TraySampleApp PRIVATE /W4 /permissive-)
else()
    target_compile_options(TraySampleApp PRIVATE -Wall -Wextra -Wpedantic)
endif()
