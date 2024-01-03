cmake_minimum_required(VERSION 3.20.0)
project(SbAudio C)

# Version
SET(LIB_MAJOR_VERSION "0")
SET(LIB_MINOR_VERSION "1")
SET(LIB_REVISION "00")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)

# TODO: Should this be committed? What do projects do?
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_PULSEAUDIO "Enable PulseAudio backend" ON)
option(ENABLE_WASAPI "Enable WASAPI backend" ON)

option(BUILD_UTILS "Build utils/ folder" OFF)
if(WIN32)
option(PLATFORM_WIN32 "Enable native Win32 platform instead of SDL2" OFF)
endif()
option(LOG_ASSERTIONS "Bind FAudio_assert to log, instead of platform's assert" OFF)
option(FORCE_ENABLE_DEBUGCONFIGURATION "Enable DebugConfiguration in all build types" OFF)
option(DUMP_VOICES "Dump voices to RIFF WAVE files" OFF)
option(BUILD_SHARED_LIBS "Build shared library" ON)

if(ENABLE_PULSEAUDIO)
    find_package(PulseAudio)
    if(PULSEAUDIO_FOUND)
        set(STATUS_PULSEAUDIO "OK")
        set(SOUNDIO_HAVE_PULSEAUDIO true)
        include_directories(${PULSEAUDIO_INCLUDE_DIR})
    else()
        set(STATUS_PULSEAUDIO "not found")
        set(SOUNDIO_HAVE_PULSEAUDIO false)
        set(PULSEAUDIO_LIBRARY "")
    endif()
else()
    set(STATUS_PULSEAUDIO "disabled")
    set(SOUNDIO_HAVE_PULSEAUDIO false)
    set(PULSEAUDIO_LIBRARY "")
endif()

if(ENABLE_WASAPI)
    find_package(WASAPI)
    if(WASAPI_FOUND)
        set(STATUS_WASAPI "OK")
        set(SOUNDIO_HAVE_WASAPI true)
    else()
        set(STATUS_WASAPI "not found")
        set(SOUNDIO_HAVE_WASAPI false)
    endif()
else()
    set(STATUS_WASAPI "disabled")
    set(SOUNDIO_HAVE_WASAPI false)
endif()

set(SOUNDIO_SOURCES
    src/soundio/channel_layout.c
    src/soundio/dummy.c
    src/soundio/os.c
    src/soundio/ring_buffer.c
    src/soundio/soundio.c
    src/soundio/util.c
)

if(SOUNDIO_HAVE_PULSEAUDIO)
    set(SOUNDIO_SOURCES ${SOUNDIO_SOURCES}
        "src/soundio/pulseaudio.c"
    )
endif()
if(SOUNDIO_HAVE_WASAPI)
    set(SOUNDIO_SOURCES ${SOUNDIO_SOURCES}
        "src/soundio/wasapi.c"
    )
endif()

# Build Type
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

# Platform Flags
if(APPLE)
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
endif()

# Should this be OBJECT instead?
add_library(SbBackend STATIC ${SOUNDIO_SOURCES})

if(SOUNDIO_HAVE_PULSEAUDIO)
    target_compile_definitions(SbBackend PRIVATE SOUNDIO_HAVE_PULSEAUDIO)
endif()
if(SOUNDIO_HAVE_WASAPI)
    target_compile_definitions(SbBackend PRIVATE SOUNDIO_HAVE_WASAPI)
endif()

# Source lists
add_library(SbAudio
    # Public Headers
    include/sb3DAudio.h
    include/sbAPOBase.h
    include/sbAPOFX.h
    include/sbAPO.h
    include/sbAudioFX.h
    include/sbAudio.h
    # Internal Headers
    src/FAudio_internal.h
    src/stb_modified.h
    src/stb_vorbis_modified.h
    src/qoa_decoder.h
    # Source Files
    src/F3DAudio.c
    src/FAPOBase.c
    src/FAPOFX.c
    src/FAPOFX_echo.c
    src/FAPOFX_eq.c
    src/FAPOFX_masteringlimiter.c
    src/FAPOFX_reverb.c
    src/FAudio.c
    src/FAudioFX_reverb.c
    src/FAudioFX_volumemeter.c
    src/FAudio_internal.c
    src/FAudio_internal_simd.c
    src/FAudio_operationset.c
    src/FAudio_platform_sdl2.c
    src/FAudio_platform_win32.c
    # Optional source files
    src/XNA_Song.c
)

target_link_libraries(SbAudio PRIVATE SbBackend)

if(PLATFORM_WIN32)
    target_link_libraries(SbAudio PRIVATE -ldxguid -luuid -lwinmm -lole32 -ladvapi32 -luser32 -lmfplat -lmfreadwrite -lmfuuid -lpropsys)
    target_compile_definitions(SbAudio PUBLIC FAUDIO_WIN32_PLATFORM)
endif()

if (FORCE_ENABLE_DEBUGCONFIGURATION)
    target_compile_definitions(SbAudio PRIVATE FAUDIO_ENABLE_DEBUGCONFIGURATION>)
else()
    # Only enable DebugConfiguration in non-release builds
    target_compile_definitions(SbAudio PRIVATE $<$<NOT:$<CONFIG:Release>>:FAUDIO_ENABLE_DEBUGCONFIGURATION>)
endif()

# FAudio_assert Customization
if(LOG_ASSERTIONS)
    target_compile_definitions(SbAudio PUBLIC FAUDIO_LOG_ASSERTIONS)
endif()

# SbAudio folders as includes, for other targets to consume
target_include_directories(SbAudio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Shared object name (soname)
set_target_properties(SbAudio PROPERTIES OUTPUT_NAME "SbAudio"
    VERSION ${LIB_VERSION}
    SOVERSION ${LIB_MAJOR_VERSION}
)

# Dump source voices to RIFF WAVE files
if(DUMP_VOICES)
    target_compile_definitions(SbAudio PRIVATE FAUDIO_DUMP_VOICES)
endif()

# SDL2 Dependency
if (PLATFORM_WIN32)
    message(STATUS "not using SDL2")
elseif (DEFINED SDL2_INCLUDE_DIRS AND DEFINED SDL2_LIBRARIES)
    message(STATUS "using pre-defined variables SDL2_INCLUDE_DIRS and SDL2_LIBRARIES")
    target_include_directories(SbAudio PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
    target_link_libraries(SbAudio PUBLIC ${SDL2_LIBRARIES})
else()
    # Try to autodetect if either of the SDL2 variables isn't explicitly set
    find_package(SDL2 CONFIG)
    if (TARGET SDL2::SDL2)
        message(STATUS "using TARGET SDL2::SDL2")
        target_link_libraries(SbAudio PUBLIC SDL2::SDL2)
    elseif (TARGET SDL2)
        message(STATUS "using TARGET SDL2")
        target_link_libraries(SbAudio PUBLIC SDL2)
    else()
        message(STATUS "no TARGET SDL2::SDL2, or SDL2, using variables")
        target_include_directories(SbAudio PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
        target_link_libraries(SbAudio PUBLIC ${SDL2_LIBRARIES})
    endif()
endif()

if(BUILD_UTILS)
    enable_language(CXX)

    # Shared ImGui Framework
    add_library(uicommon STATIC
        utils/uicommon/FAudioUI_main.cpp
        utils/uicommon/imconfig.h
        utils/uicommon/imgui.cpp
        utils/uicommon/imgui_demo.cpp
        utils/uicommon/imgui_draw.cpp
        utils/uicommon/imgui_widgets.cpp
        utils/uicommon/imgui.h
        utils/uicommon/imgui_internal.h
        utils/uicommon/imstb_rectpack.h
        utils/uicommon/imstb_textedit.h
        utils/uicommon/imstb_truetype.h
    )
    target_link_libraries(uicommon PUBLIC SbAudio)

    # Shared WAV Resources
    add_library(wavs STATIC utils/wavcommon/wavs.cpp)
    target_compile_definitions(wavs PUBLIC
        RESOURCE_PATH="${CMAKE_SOURCE_DIR}/utils/wavcommon/resources"
    )

    # These tools do NOT use uicommon
    add_executable(testxwma utils/testxwma/testxwma.cpp)
    target_link_libraries(testxwma PRIVATE SbAudio)
    add_executable(showriffheader utils/showriffheader/showriffheader.cpp)
    target_link_libraries(showriffheader PRIVATE SbAudio)

    # These tools use uicommon, but NOT wavs
    add_executable(testfilter
        utils/testfilter/audio.cpp
        utils/testfilter/audio_faudio.cpp
        utils/testfilter/audio.h
        utils/testfilter/audio_player.h
        utils/testfilter/audio_xaudio.cpp
        utils/testfilter/oscillator.cpp
        utils/testfilter/oscillator.h
        utils/testfilter/testfilter.cpp
    )
    target_link_libraries(testfilter PRIVATE uicommon)

    # These tools use both uicommon and wavs
    add_executable(testreverb
        utils/testreverb/audio.cpp
        utils/testreverb/audio_faudio.cpp
        utils/testreverb/audio.h
        utils/testreverb/audio_xaudio.cpp
        utils/testreverb/testreverb.cpp
    )
    target_link_libraries(testreverb PRIVATE uicommon wavs)
    add_executable(testvolumemeter
        utils/testvolumemeter/audio.cpp
        utils/testvolumemeter/audio_faudio.cpp
        utils/testvolumemeter/audio.h
        utils/testvolumemeter/testvolumemeter.cpp
    )
    target_link_libraries(testvolumemeter PRIVATE uicommon wavs)
    add_executable(voicepool
        utils/voicepool/voicepool.cpp
    )
    target_link_libraries(voicepool PRIVATE uicommon wavs)
endif()

# define install directories
# on mingw-w64 cross compilation $CMAKE_INSTALL_LIBDIR is set to an absolute
# path. Work around that by hard coding the directories on windows
if(WIN32)
    set(SbAudio_INSTALL_INCLUDEDIR include)
    set(SbAudio_INSTALL_BINDIR bin)
    set(SbAudio_INSTALL_LIBDIR lib)
else()
    include(GNUInstallDirs)
    set(SbAudio_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_INCLUDEDIR})
    set(SbAudio_INSTALL_BINDIR ${CMAKE_INSTALL_BINDIR})
    set(SbAudio_INSTALL_LIBDIR ${CMAKE_INSTALL_LIBDIR})
endif()

# Installation

# Public Headers...
install(
    DIRECTORY include/
    DESTINATION ${SbAudio_INSTALL_INCLUDEDIR}
)
# Libraries...
install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    INCLUDES DESTINATION ${SbAudio_INSTALL_INCLUDEDIR}
    RUNTIME DESTINATION ${SbAudio_INSTALL_BINDIR}
    LIBRARY DESTINATION ${SbAudio_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${SbAudio_INSTALL_LIBDIR}
)

# Generate cmake-config file, install CMake files
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${SbAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/${PROJECT_NAME}Config.cmake
    DESTINATION ${SbAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
install(
    EXPORT ${PROJECT_NAME}-targets
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION ${SbAudio_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)
