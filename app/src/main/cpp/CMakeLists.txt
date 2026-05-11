cmake_minimum_required(VERSION 3.22.1)
project("learnffmpeg")

set(CMAKE_CXX_STANDARD 17)

# Path to FFmpeg headers (committed under app/libs) and prebuilt .so libs
# (under app/src/main/jniLibs/<ABI>/ via ${ANDROID_ABI}).
set(FFMPEG_INCLUDE "${CMAKE_SOURCE_DIR}/../../../libs/ffmpeg/include")
set(FFMPEG_LIB_DIR "${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}")

include_directories(${FFMPEG_INCLUDE})

# Declare prebuilt FFmpeg shared libraries as IMPORTED targets so we can link them.
set(FFMPEG_LIBS avformat avcodec avutil swscale swresample)
foreach(lib ${FFMPEG_LIBS})
    add_library(${lib} SHARED IMPORTED)
    set_target_properties(${lib} PROPERTIES
            IMPORTED_LOCATION "${FFMPEG_LIB_DIR}/lib${lib}.so")
endforeach()

add_library(${CMAKE_PROJECT_NAME} SHARED
        native-lib.cpp
        ff_player.cpp
        audio_renderer.cpp)

find_library(log-lib log)
find_library(android-lib android)
find_library(opensl-lib OpenSLES)

target_link_libraries(${CMAKE_PROJECT_NAME}
        ${FFMPEG_LIBS}
        ${opensl-lib}
        ${android-lib}
        ${log-lib})
