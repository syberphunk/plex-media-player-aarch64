cmake_minimum_required(VERSION 3.16...3.31)
project(plex-bridge-harness CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt5 REQUIRED COMPONENTS Core Widgets WebChannel WebEngineWidgets)

add_executable(bridge-harness main.cpp)
target_link_libraries(bridge-harness
  Qt5::Core Qt5::Widgets Qt5::WebChannel Qt5::WebEngineWidgets)
