cmake_minimum_required(VERSION 3.20)
project(mania_player VERSION 1.0.0 LANGUAGES CXX C)

# ============================================================================
# C++ Standard
# ============================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Build Type (default to Release)
# ============================================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ============================================================================
# Platform Detection
# ============================================================================
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
elseif(UNIX)
    set(PLATFORM_LINUX TRUE)
endif()

message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# ============================================================================
# Source Files
# ============================================================================
file(GLOB_RECURSE SOURCES
    "src/core/*.cpp"
    "src/parsers/*.cpp"
    "src/graphics/*.cpp"
    "src/audio/*.cpp"
    "src/systems/*.cpp"
)

# Third-party C sources
set(LZMA_SOURCES
    third_party/lzma/LzmaEnc.c
    third_party/lzma/LzmaDec.c
    third_party/lzma/LzFind.c
    third_party/lzma/CpuArch.c
    third_party/lzma/LzFindMt.c
    third_party/lzma/Threads.c
    third_party/lzma/LzFindOpt.c
)

set(MINILZO_SOURCES
    third_party/minilzo/minilzo.c
)

# ============================================================================
# Executable
# ============================================================================
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${LZMA_SOURCES}
    ${MINILZO_SOURCES}
)

# ============================================================================
# Include Directories
# ============================================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/parsers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/src/systems
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bass
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzma
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minilzo
)

# Platform-specific includes
if(PLATFORM_WINDOWS)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/icu
        ${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg
    )
endif()

# ============================================================================
# Compiler Options
# ============================================================================
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /utf-8           # UTF-8 source and execution charset
        /bigobj          # Allow large object files (Game.cpp + inline DBs)
        /wd4819          # Disable warning about codepage
        /EHsc            # Exception handling
        /O2              # Optimize for speed (Release)
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX         # Prevent Windows.h min/max macros
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Debug>:-g>
    )
endif()

# ============================================================================
# Platform-Specific Configuration
# ============================================================================
if(PLATFORM_WINDOWS)
    # Windows subsystem (no console window)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    # Library path for pre-built Windows libraries
    target_link_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/x64
    )

    # Link Windows libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        # SDL
        SDL3
        SDL3_ttf
        # Audio
        bass
        bass_fx
        # ICU
        icuuc
        icuin
        # FFmpeg
        avcodec
        avformat
        avutil
        swscale
        swresample
        # Windows system libraries
        shell32
        advapi32
        comdlg32
    )

elseif(PLATFORM_MACOS)
    # Find frameworks and libraries on macOS
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)

    # TODO: Set paths to your macOS libraries
    # You'll need to download macOS versions of:
    # - SDL3, SDL3_ttf
    # - BASS, bass_fx
    # - ICU
    # - FFmpeg

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        # Add library paths here after downloading macOS versions
    )

elseif(PLATFORM_LINUX)
    # Find packages using pkg-config
    find_package(PkgConfig REQUIRED)

    # SDL3
    pkg_check_modules(SDL3 REQUIRED sdl3)
    pkg_check_modules(SDL3_TTF REQUIRED sdl3-ttf)

    # FFmpeg
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

    # ICU
    pkg_check_modules(ICU REQUIRED icu-uc icu-i18n)

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${SDL3_INCLUDE_DIRS}
        ${SDL3_TTF_INCLUDE_DIRS}
        ${AVCODEC_INCLUDE_DIRS}
        ${ICU_INCLUDE_DIRS}
    )

    target_link_directories(${PROJECT_NAME} PRIVATE
        ${ICU_LIBRARY_DIRS}
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${SDL3_LIBRARIES}
        ${SDL3_TTF_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVFORMAT_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES}
        icuuc
        icui18n
        pthread
        bass
        bass_fx
    )
endif()

# ============================================================================
# Output Directory
# ============================================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

# ============================================================================
# Copy DLLs/dylibs to output directory (Windows/macOS)
# ============================================================================
# NOTE: DLLs are not in lib/x64, they should be copied manually or placed
# in a directory that's in the system PATH.
# If you have DLLs in a specific directory, uncomment and modify below:
#
# if(PLATFORM_WINDOWS)
#     add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different
#             "path/to/your/dlls/*.dll"
#             "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
#         COMMENT "Copying DLLs to output directory"
#     )
# endif()

# ============================================================================
# Install Rules
# ============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

if(PLATFORM_WINDOWS)
    install(FILES
        lib/x64/SDL3.dll
        lib/x64/SDL3_ttf.dll
        lib/x64/bass.dll
        lib/x64/bass_fx.dll
        lib/x64/icuuc75.dll
        lib/x64/icuin75.dll
        lib/x64/icudt75.dll
        lib/x64/avcodec-61.dll
        lib/x64/avformat-61.dll
        lib/x64/avutil-59.dll
        lib/x64/swscale-8.dll
        lib/x64/swresample-5.dll
        DESTINATION bin
    )
endif()

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "=== Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output: ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}")
message(STATUS "===========================")
