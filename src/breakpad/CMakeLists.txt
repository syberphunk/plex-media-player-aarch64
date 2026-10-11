add_sources(BreakPad.h)

if(BREAKPAD_FOUND)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # requires Linux kernel and glibc
    add_sources(BreakPadLinux.cpp)
  else()
    add_sources(BreakPadDummy.cpp)
  endif()
else()
  add_sources(BreakPadDummy.cpp)
endif()
