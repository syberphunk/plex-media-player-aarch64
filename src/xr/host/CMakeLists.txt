add_executable(pmp-xr-host main.cpp OpenVr.cpp Present.cpp)
set_target_properties(pmp-xr-host PROPERTIES AUTOMOC OFF)
target_link_libraries(pmp-xr-host ${CMAKE_DL_LIBS})

install(TARGETS pmp-xr-host DESTINATION ${INSTALL_BIN_DIR})
