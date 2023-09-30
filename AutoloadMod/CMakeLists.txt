cmake_minimum_required(VERSION 3.18)

set(TARGET AutoloadMod)
project(${TARGET})

add_library(${TARGET} SHARED "dllmain.cpp" "Autoload.cpp")
target_include_directories(${TARGET} PRIVATE .)
target_link_libraries(${TARGET} PUBLIC UE4SS)
