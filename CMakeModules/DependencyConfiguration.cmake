find_package(PkgConfig)

include(FetchDependencies)

set(DEPENDENCY_ROOT ${DEFAULT_ROOT} CACHE PATH "Path where the deps are located")

if(IS_DIRECTORY ${DEPENDENCY_ROOT})
  message(STATUS "Going to use bundled deps in directory: ${DEPENDENCY_ROOT}")
  list(APPEND CMAKE_FIND_ROOT_PATH ${DEPENDENCY_ROOT})
  list(APPEND CMAKE_PREFIX_PATH ${DEPENDENCY_ROOT})
  set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_FIND_ROOT_PATH}/lib/pkgconfig)
  set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH TRUE)
  include_directories(${DEPENDENCY_ROOT}/include)
else(IS_DIRECTORY ${DEPENDENCY_ROOT})
  message(STATUS "Not using bundled deps")
endif(IS_DIRECTORY ${DEPENDENCY_ROOT})

find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
