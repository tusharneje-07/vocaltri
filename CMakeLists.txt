cmake_minimum_required(VERSION 3.24)

project(vocaltri VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network)
include(CTest)

add_library(vocaltri-common STATIC
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
)
target_link_libraries(vocaltri PRIVATE vocaltri-common Qt6::Core Qt6::Network)
target_compile_features(vocaltri PRIVATE cxx_std_23)
target_compile_definitions(vocaltri PRIVATE VOCALTRI_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
    add_executable(vocaltri-protocol-test tests/protocol_test.cpp)
    target_link_libraries(vocaltri-protocol-test PRIVATE vocaltri-common Qt6::Core Qt6::Network)
    add_test(NAME vocaltri-protocol COMMAND vocaltri-protocol-test)
endif()

install(TARGETS vocaltri RUNTIME DESTINATION bin)
install(FILES extra/vocaltri.service DESTINATION lib/systemd/user)
