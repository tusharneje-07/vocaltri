cmake_minimum_required(VERSION 3.24)

project(vocaltri VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
find_package(Qt6 6.5 QUIET COMPONENTS Multimedia WebSockets)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        pkg_check_modules(LIBEI QUIET IMPORTED_TARGET libei-1.0)
        pkg_check_modules(LIBOEFFIS QUIET IMPORTED_TARGET liboeffis-1.0)
        pkg_check_modules(XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
        if(LIBEI_FOUND)
            include(CheckCXXSourceCompiles)
            set(CMAKE_REQUIRED_INCLUDES ${LIBEI_INCLUDE_DIRS})
            set(CMAKE_REQUIRED_LIBRARIES ${LIBEI_LINK_LIBRARIES})
            check_cxx_source_compiles("#include <libei.h>\nint main() { ei_device_text_keysym(nullptr, 0, false); return EI_DEVICE_CAP_TEXT; }"
                                      VOCALTRI_LIBEI_HAS_TEXT_API)
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_LIBRARIES)
        endif()
    endif()
endif()

include(CTest)

add_library(vocaltri-common STATIC
    src/common/app_settings.cpp
    src/common/app_settings.hpp
    src/common/deepgram_key.cpp
    src/common/deepgram_key.hpp
    src/common/ipc_protocol.cpp
    src/common/ipc_protocol.hpp
    src/common/socket_path.cpp
    src/common/socket_path.hpp
)
target_link_libraries(vocaltri-common PUBLIC Qt6::Core Qt6::Network)
target_include_directories(vocaltri-common PUBLIC src)
target_compile_features(vocaltri-common PUBLIC cxx_std_23)

add_executable(vocaltri
    src/main.cpp
    src/daemon/daemon.cpp
    src/daemon/daemon.hpp
    src/cli/cli_client.cpp
    src/cli/cli_client.hpp
    src/ui/overlay_window.cpp
    src/ui/overlay_window.hpp
    src/ui/session_view_model.cpp
    src/ui/session_view_model.hpp
    src/input/text_injector.cpp
    src/input/text_injector.hpp
    src/ui/qml.qrc
)
target_link_libraries(vocaltri PRIVATE vocaltri-common Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick)
target_compile_features(vocaltri PRIVATE cxx_std_23)
target_compile_definitions(vocaltri PRIVATE VOCALTRI_VERSION="${PROJECT_VERSION}")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(LIBEI_FOUND AND LIBOEFFIS_FOUND AND XKBCOMMON_FOUND)
        target_link_libraries(vocaltri PRIVATE
            PkgConfig::LIBEI
            PkgConfig::LIBOEFFIS
            PkgConfig::XKBCOMMON
        )
        target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_LIBEI=1)
        if(VOCALTRI_LIBEI_HAS_TEXT_API)
            target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_EI_TEXT=1)
        else()
            target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_EI_TEXT=0)
        endif()
        message(STATUS "Native Wayland input enabled (libei/liboeffis/xkbcommon)")
    else()
        target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_LIBEI=0 VOCALTRI_HAS_EI_TEXT=0)
        message(STATUS "libei/liboeffis/xkbcommon development files not found; native Wayland input disabled, existing input backends remain available")
    endif()
else()
    target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_LIBEI=0 VOCALTRI_HAS_EI_TEXT=0)
endif()

if(TARGET Qt6::Multimedia AND TARGET Qt6::WebSockets)
    target_sources(vocaltri PRIVATE
        src/audio/audio_recorder.cpp
        src/audio/audio_recorder.hpp
        src/transcription/deepgram_client.cpp
        src/transcription/deepgram_client.hpp
        src/transcription/streaming_session.cpp
        src/transcription/streaming_session.hpp
    )
    target_link_libraries(vocaltri PRIVATE Qt6::Multimedia Qt6::WebSockets)
    target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_STREAMING=1)

    if(PIPEWIRE_FOUND)
        target_sources(vocaltri PRIVATE
            src/audio/pipewire_capture.cpp
            src/audio/pipewire_capture.hpp
        )
        target_link_libraries(vocaltri PRIVATE PkgConfig::PIPEWIRE)
        target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_PIPEWIRE=1)
    else()
        message(STATUS "libpipewire not found; audio capture falls back to Qt Multimedia")
        target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_PIPEWIRE=0)
    endif()
else()
    message(WARNING "Qt Multimedia and/or WebSockets not found; streaming capture is disabled")
    target_compile_definitions(vocaltri PRIVATE VOCALTRI_HAS_STREAMING=0)
endif()

if(BUILD_TESTING)
    add_executable(vocaltri-protocol-test tests/protocol_test.cpp)
    target_link_libraries(vocaltri-protocol-test PRIVATE vocaltri-common Qt6::Core Qt6::Network)
    add_test(NAME vocaltri-protocol COMMAND vocaltri-protocol-test)
endif()

install(TARGETS vocaltri RUNTIME DESTINATION bin)
install(FILES extra/vocaltri.service DESTINATION lib/systemd/user)
install(FILES extra/config.toml.example DESTINATION share/vocaltri)
