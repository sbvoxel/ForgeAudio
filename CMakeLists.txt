cmake_minimum_required(VERSION 3.20.0)
project(ForgeAudio C)

# Version
SET(LIB_MAJOR_VERSION "0")
SET(LIB_MINOR_VERSION "1")
SET(LIB_REVISION "00")
SET(LIB_ABI_VERSION "2")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)

# TODO: Should this be committed? What do projects do?
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_WASAPI "Enable WASAPI backend" ON)

option(BUILD_UTILS "Build utils/ folder" OFF)
if(WIN32)
option(PLATFORM_WIN32 "Enable native Win32 platform instead of SDL3" OFF)
endif()
option(LOG_ASSERTIONS "Bind ForgeAudio_assert to log, instead of platform's assert" OFF)
option(FORCE_ENABLE_DEBUGCONFIGURATION "Enable DebugConfiguration in all build types" OFF)
option(DUMP_VOICES "Dump voices to RIFF WAVE files" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)

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
    include/forge_spatial_audio.h
    include/forge_effect.h
    include/forge_effects.h
    include/forge_audio.h
    # Internal Headers
    src/forge_effect_base_internal.h
    src/forge_audio_internal.h
    src/stb_modified.h
    # Source Files
    src/forge_spatial_audio.c
    src/forge_effect_base.c
    src/forge_audio.c
    src/forge_audio_fx_reverb.c
    src/forge_audio_fx_volume_meter.c
    src/forge_audio_internal.c
    src/forge_audio_internal_simd.c
    src/forge_audio_operation_set.c
    src/forge_audio_platform_sdl3.c
    src/forge_audio_platform_win32.c
)

if(PLATFORM_WIN32)
    target_link_libraries(ForgeAudio PRIVATE dxguid uuid winmm ole32 advapi32 user32 mfplat mfreadwrite mfuuid propsys)
    target_compile_definitions(ForgeAudio PUBLIC FORGE_AUDIO_WIN32_PLATFORM)
endif()

if (FORCE_ENABLE_DEBUGCONFIGURATION)
    target_compile_definitions(ForgeAudio PRIVATE FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION>)
else()
    # Only enable DebugConfiguration in non-release builds
    target_compile_definitions(ForgeAudio PRIVATE $<$<NOT:$<CONFIG:Release>>:FORGE_AUDIO_ENABLE_DEBUGCONFIGURATION>)
endif()

# ForgeAudio_assert Customization
if(LOG_ASSERTIONS)
    target_compile_definitions(ForgeAudio PUBLIC FORGE_AUDIO_LOG_ASSERTIONS)
endif()

# ForgeAudio folders as includes, for other targets to consume
target_include_directories(ForgeAudio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Shared object name (soname)
set_target_properties(ForgeAudio PROPERTIES OUTPUT_NAME "ForgeAudio"
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_ABI_VERSION}
)

# Dump source voices to RIFF WAVE files
if(DUMP_VOICES)
    target_compile_definitions(ForgeAudio PRIVATE FORGE_AUDIO_DUMP_VOICES)
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

if(BUILD_UTILS)
    enable_language(CXX)

    add_executable(showriffheader utils/showriffheader/showriffheader.cpp)
    target_link_libraries(showriffheader PRIVATE ForgeAudio)
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
