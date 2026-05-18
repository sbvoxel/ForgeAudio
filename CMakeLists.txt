cmake_minimum_required(VERSION 3.20.0)
project(ForgeAudio C)

# Version
SET(LIB_MAJOR_VERSION "0")
SET(LIB_MINOR_VERSION "1")
SET(LIB_REVISION "00")
SET(LIB_ABI_VERSION "3")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_WASAPI "Enable WASAPI backend" ON)

if(WIN32)
option(PLATFORM_WIN32 "Enable native Win32 platform instead of SDL3" OFF)
endif()
option(LOG_ASSERTIONS "Bind ForgeAudio_assert to log, instead of platform's assert" OFF)
option(FORCE_ENABLE_DEBUGCONFIGURATION "Enable DebugConfiguration in all build types" OFF)
option(DUMP_VOICES "Dump voices to RIFF WAVE files" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)
option(BUILD_TESTS "Build tests" OFF)

function(forge_audio_enable_asan target)
    if(NOT ENABLE_ASAN)
        return()
    endif()

    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
    else()
        message(FATAL_ERROR "ENABLE_ASAN is not supported for ${CMAKE_C_COMPILER_ID}")
    endif()
endfunction()

# Build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # By default, we use Release
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE
        STRING "Choose the type of build." FORCE
    )

    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
        STRINGS "Debug" "Release" "RelWithDebInfo"
    )
endif()

# Platform flags
if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
endif()

# Source lists
add_library(ForgeAudio
    # Public Headers
    include/forge/spatial_audio.h
    include/forge/effect.h
    include/forge/effects.h
    include/forge/audio.h
    # Internal Headers
    src/effect_internal.h
    src/effect_base_internal.h
    src/common_internal.h
    src/format_internal.h
    src/platform_internal.h
    src/util_internal.h
    src/core_internal.h
    src/debug_internal.h
    src/simd_internal.h
    src/batch_internal.h
    src/dump_internal.h
    # Source Files
    src/spatial_audio.c
    src/effect_base.c
    src/audio.c
    src/biquad.c
    src/delay.c
    src/limiter.c
    src/reverb.c
    src/volume_meter.c
    src/debug.c
    src/decode.c
    src/engine.c
    src/format.c
    src/util.c
    src/audio_simd.c
    src/audio_batch.c
    src/platform_sdl3.c
    src/platform_win32.c
)
forge_audio_enable_asan(ForgeAudio)

if(PLATFORM_WIN32)
    target_link_libraries(ForgeAudio PRIVATE dxguid uuid winmm ole32 advapi32 user32 mfplat mfreadwrite mfuuid propsys)
    target_compile_definitions(ForgeAudio PUBLIC FORGE_AUDIO_WIN32_PLATFORM)
endif()

if (FORCE_ENABLE_DEBUGCONFIGURATION)
    target_compile_definitions(ForgeAudio PRIVATE FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION)
else()
    # Only enable DebugConfiguration in non-release builds
    target_compile_definitions(ForgeAudio PRIVATE $<$<NOT:$<CONFIG:Release>>:FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION>)
endif()

# ForgeAudio_assert Customization
if(LOG_ASSERTIONS)
    target_compile_definitions(ForgeAudio PUBLIC FORGE_AUDIO_LOG_ASSERTIONS)
endif()

# ForgeAudio include directories
target_include_directories(ForgeAudio
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Shared object name (soname)
set_target_properties(ForgeAudio PROPERTIES OUTPUT_NAME "ForgeAudio"
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_ABI_VERSION}
)

if(NOT WIN32)
    set_target_properties(ForgeAudio PROPERTIES C_VISIBILITY_PRESET hidden)
endif()

# Dump source voices to RIFF WAVE files
if(DUMP_VOICES)
    target_compile_definitions(ForgeAudio PRIVATE FORGE_AUDIO_DUMP_VOICES)
endif()

if(BUILD_TESTS)
    enable_testing()
    target_compile_definitions(ForgeAudio PRIVATE FORGE_AUDIO_TESTING)

    add_executable(effect_chain_scratch_test tests/effect_chain_scratch.c)
    target_compile_definitions(effect_chain_scratch_test PRIVATE FORGE_AUDIO_TESTING)
    target_include_directories(effect_chain_scratch_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(effect_chain_scratch_test PRIVATE ForgeAudio)
    forge_audio_enable_asan(effect_chain_scratch_test)
    add_test(NAME effect_chain_scratch COMMAND effect_chain_scratch_test)

    add_executable(source_resampler_test tests/source_resampler.c)
    target_compile_definitions(source_resampler_test PRIVATE FORGE_AUDIO_TESTING)
    target_include_directories(source_resampler_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(source_resampler_test PRIVATE ForgeAudio)
    forge_audio_enable_asan(source_resampler_test)
    add_test(NAME source_resampler COMMAND source_resampler_test)

    add_executable(source_format_test tests/source_format.c)
    target_compile_definitions(source_format_test PRIVATE FORGE_AUDIO_TESTING)
    target_include_directories(source_format_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(source_format_test PRIVATE ForgeAudio)
    forge_audio_enable_asan(source_format_test)
    add_test(NAME source_format COMMAND source_format_test)

    add_executable(engine_render_test
        tests/audio_render_harness.c
        tests/audio_render_harness.h
        tests/engine_render.c
        tests/engine_render_biquad.c
        tests/engine_render_channel.c
        tests/engine_render_common.c
        tests/engine_render_core.c
        tests/engine_render_delay.c
        tests/engine_render_fade_stop.c
        tests/engine_render_filter.c
        tests/engine_render_gain.c
        tests/engine_render_limiter.c
        tests/engine_render_output_matrix.c
        tests/engine_render_reverb.c
        tests/engine_render_tests.h
        tests/engine_render_volume.c
    )
    target_compile_definitions(engine_render_test PRIVATE FORGE_AUDIO_TESTING)
    target_include_directories(engine_render_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(engine_render_test PRIVATE ForgeAudio)
    forge_audio_enable_asan(engine_render_test)
    add_test(NAME engine_render COMMAND engine_render_test)
endif()

# SDL3 Dependency
if (PLATFORM_WIN32)
    message(STATUS "not using SDL3")
elseif (DEFINED SDL3_INCLUDE_DIRS AND DEFINED SDL3_LIBRARIES)
    message(STATUS "using pre-defined variables SDL3_INCLUDE_DIRS and SDL3_LIBRARIES")
    target_include_directories(ForgeAudio PUBLIC "$<BUILD_INTERFACE:${SDL3_INCLUDE_DIRS}>")
    target_link_libraries(ForgeAudio PUBLIC ${SDL3_LIBRARIES})
else()
    # Try to autodetect if either of the SDL3 variables isn't explicitly set
    find_package(SDL3 CONFIG)
    if (TARGET SDL3::SDL3)
        message(STATUS "using TARGET SDL3::SDL3")
        target_link_libraries(ForgeAudio PUBLIC SDL3::SDL3)
    elseif (TARGET SDL3)
        message(STATUS "using TARGET SDL3")
        target_link_libraries(ForgeAudio PUBLIC SDL3)
    else()
        message(STATUS "no TARGET SDL3::SDL3, or SDL3, using variables")
        target_include_directories(ForgeAudio PUBLIC "$<BUILD_INTERFACE:${SDL3_INCLUDE_DIRS}>")
        target_link_libraries(ForgeAudio PUBLIC ${SDL3_LIBRARIES})
    endif()
endif()

# define install directories
# on mingw-w64 cross compilation $CMAKE_INSTALL_LIBDIR is set to an absolute
# path. Work around that by hard coding the directories on windows
if(WIN32)
    set(ForgeAudio_INSTALL_INCLUDEDIR include)
    set(ForgeAudio_INSTALL_BINDIR bin)
    set(ForgeAudio_INSTALL_LIBDIR lib)
else()
    include(GNUInstallDirs)
    set(ForgeAudio_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(ForgeAudio_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(ForgeAudio_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

# Installation

# Public Headers...
install(
    DIRECTORY include/
    DESTINATION ${ForgeAudio_INSTALL_INCLUDEDIR}
)
# Libraries...
install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    INCLUDES DESTINATION ${ForgeAudio_INSTALL_INCLUDEDIR}
    RUNTIME DESTINATION ${ForgeAudio_INSTALL_BINDIR}
    LIBRARY DESTINATION ${ForgeAudio_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${ForgeAudio_INSTALL_LIBDIR}
)

# Generate cmake-config file, install CMake files
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${ForgeAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}Config.cmake
    DESTINATION ${ForgeAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
install(
    EXPORT ${PROJECT_NAME}-targets
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${ForgeAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
