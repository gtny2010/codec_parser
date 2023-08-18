cmake_minimum_required(VERSION 3.14)
project(TopsVideoParse)

set(CMAKE_CXX_STANDARD 14)

# set(CMAKE_BUILD_TYPE Release)
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g -pg -fpermissive -pthread")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin/)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/)

if(CMAKE_EXTERNAL_RELEASE)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
endif()

add_compile_options(-D_GLIBCXX_USE_CXX14_ABI=0)
add_compile_options(-g)
add_compile_options(-fPIC)



add_subdirectory(src)