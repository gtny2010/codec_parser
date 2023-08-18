add_compile_options(-D_GLIBCXX_USE_CXX11_ABI=0)
# include_directories(${PROJECT_SOURCE_DIR}/3rdParty/ffmpeg-n4.5/include)
# link_directories(${PROJECT_SOURCE_DIR}/3rdParty/ffmpeg-n4.5/lib)
# include_directories(/usr/local/include) 
# link_directories(/usr/local/lib)
# include(${PROJECT_SOURCE_DIR}/3rd/ffmpeg.cmake)
# set(FFMPEG_LIBRARIES avformat avcodec avutil swscale avdevice swresample z m lzma bz2 pthread)

set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=gold")

# include_directories(${CMAKE_CURRENT_SOURCE_DIR})
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/utils)

# set(CMAKE_EXE_LINKER_FLAGS "-static")
include_directories(${CMAKE_CURRENT_BINARY_DIR})
file(GLOB  SOURCES *.c)

add_executable(TopsVideoDecParser ${SOURCES})
target_link_libraries(TopsVideoDecParser PUBLIC
pthread
)


