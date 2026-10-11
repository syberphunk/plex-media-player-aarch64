include_directories(${PROJECT_SOURCE_DIR}/src)

add_executable(socket-client
  SocketClient.cpp
  $<TARGET_OBJECTS:shared>
)
std_target_properties(socket-client)
target_link_libraries(socket-client Qt6::Network Qt6::Core qslog)
