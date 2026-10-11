cmake_minimum_required(VERSION 3.16...3.31)
project(plex-bridge-harness CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebChannel WebEngineWidgets)

add_executable(bridge-harness main.cpp)
target_link_libraries(bridge-harness
  Qt6::Core Qt6::Widgets Qt6::WebChannel Qt6::WebEngineWidgets)
