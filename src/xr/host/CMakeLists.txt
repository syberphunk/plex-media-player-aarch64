add_executable(pmp-xr-host main.cpp OpenVr.cpp Present.cpp RootProperty.cpp)
set_target_properties(pmp-xr-host PROPERTIES AUTOMOC OFF)
target_link_libraries(pmp-xr-host ${CMAKE_DL_LIBS})

if(X11_FOUND)
  target_compile_definitions(pmp-xr-host PRIVATE PMP_XR_HOST_X11)
  target_include_directories(pmp-xr-host PRIVATE ${X11_INCLUDE_DIR})
  target_link_libraries(pmp-xr-host ${X11_X11_LIB})
endif()

install(TARGETS pmp-xr-host DESTINATION ${INSTALL_BIN_DIR})
