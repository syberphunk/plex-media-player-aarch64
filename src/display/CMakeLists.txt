if(BUILD_TARGET STREQUAL "RPI")
  add_subdirectory(rpi)
elseif(X11XRANDR_FOUND)
  add_subdirectory(x11)
endif()

add_subdirectory(dummy)

find_all_sources(. SRC)
add_sources(${SRC})