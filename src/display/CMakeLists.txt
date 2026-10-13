if(X11XRANDR_FOUND)
  add_subdirectory(x11)
endif()

add_subdirectory(dummy)

find_all_sources(. SRC)
add_sources(${SRC})