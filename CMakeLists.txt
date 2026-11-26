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
