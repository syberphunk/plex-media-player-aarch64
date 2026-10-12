include_directories(
  ${CMAKE_SOURCE_DIR}/external/qhttp/src
  ${CMAKE_SOURCE_DIR}/external/qslog
  ${CMAKE_SOURCE_DIR}/src/player
  core
  shared
)

set(CMAKE_AUTOMOC ON)

add_definitions(-DPREFIX="${CMAKE_INSTALL_PREFIX}")

find_package(Breakpad)

if(BREAKPAD_FOUND)
  include_directories(${BREAKPAD_INCLUDE_DIR})
  set(BREAKPAD_LIBRARIES ${BREAKPAD_LIBRARY})
  if(UNIX)
    # cmake issue: breakpad-client.pc adds this, but cmake discards it
    set(BREAKPAD_LIBRARIES ${BREAKPAD_LIBRARIES} -lpthread)
  endif()
endif()

add_subdirectory(shared)
add_subdirectory(core)
add_subdirectory(display)
add_subdirectory(player)
add_subdirectory(utils)
add_subdirectory(server)
add_subdirectory(ui)
add_subdirectory(input)
add_subdirectory(system)
add_subdirectory(breakpad)
add_subdirectory(settings)
add_subdirectory(power)
add_subdirectory(remote)
add_subdirectory(connector)
add_subdirectory(tools)

get_property(ALL_SRCS GLOBAL PROPERTY SRCS_LIST)
set(MAIN_SRCS main.cpp)

source_group("Source Files" FILES ${MAIN_SRCS})
set(SOURCES ${MAIN_SRCS} ${ALL_SRCS})

foreach(sfile in ${ALL_SRCS})
  get_filename_component(REALNAME ${sfile} REALPATH)
  get_filename_component(DIRNAME ${REALNAME} DIRECTORY)
  string(REPLACE "${CMAKE_SOURCE_DIR}/src/" "" SUBDIR ${DIRNAME})
  string(TOLOWER ${SUBDIR} SUBDIR)
  string(REPLACE "/" "\\\\" GNAME ${SUBDIR})
  source_group("Source Files\\\\${GNAME}" FILES ${sfile})
endforeach(sfile in ${ALL_SRCS})

file(GLOB_RECURSE RESOURCE_FILES ${CMAKE_SOURCE_DIR}/resources/*)

# generate resources.qrc from directory tree listings
add_custom_command(OUTPUT resources.qrc
  COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/build-qt-resources.py
          ${CMAKE_CURRENT_BINARY_DIR}/resources.qrc
          /=${CMAKE_SOURCE_DIR}/resources/
          ui/webview.qml=${CMAKE_SOURCE_DIR}/src/ui/webview.qml
  COMMENT "Creating resources.qrc"
  DEPENDS ${CMAKE_SOURCE_DIR}/scripts/build-qt-resources.py
    ${RESOURCE_FILES}
    ${CMAKE_SOURCE_DIR}/src/ui/webview.qml
)
set_source_files_properties(resources.qrc PROPERTIES GENERATED TRUE)
add_custom_command(OUTPUT qrc_resources.cpp
  COMMAND Qt6::rcc -name resources -o qrc_resources.cpp resources.qrc
  DEPENDS resources.qrc ${CMAKE_SOURCE_DIR}/src/ui/webview.qml ${RESOURCE_FILES}
)
set_source_files_properties(qrc_resources.cpp PROPERTIES GENERATED TRUE)

list(APPEND RESOURCE_FILES qrc_resources.cpp)

set(RESOURCE_ROOT .)

if(WEB_TV_DIR)
  add_resources(TARGET ${MAIN_TARGET} SOURCES ${WEB_TV_DIR} DEST ${RESOURCE_ROOT}/web-client/tv)
  if(NOT WEB_CLIENT_DISABLE_DESKTOP)
    add_resources(TARGET ${MAIN_TARGET} SOURCES ${WEB_DESKTOP_DIR} DEST ${RESOURCE_ROOT}/web-client/desktop)
  endif()
endif()

set(rsrc_locations
  ${QTROOT}/resources
  /usr/share/qt/resources
)
foreach(loc ${rsrc_locations})
  if(EXISTS ${loc}/qtwebengine_devtools_resources.pak)
    install(FILES ${loc}/qtwebengine_devtools_resources.pak DESTINATION resources)
  endif()
endforeach()
if(WEB_TV_DIR)
  install(DIRECTORY ${WEB_TV_DIR}/ DESTINATION ${INSTALL_RESOURCE_DIR}/web-client/tv)
  install(DIRECTORY ${WEB_DESKTOP_DIR}/ DESTINATION ${INSTALL_RESOURCE_DIR}/web-client/desktop)
endif()

get_property(BUNDLED_FILES GLOBAL PROPERTY CONFIG_BUNDLED_FILES)
add_executable(${MAIN_TARGET} ${SOURCES} ${BUNDLED_FILES} ${RESOURCE_FILES})
std_target_properties(${MAIN_TARGET})
set_target_properties(${MAIN_TARGET} PROPERTIES
  INSTALL_RPATH "${QTROOT}/lib"
  OUTPUT_NAME ${MAIN_NAME}
)
copy_resources(${MAIN_TARGET})
clang_tidy(${MAIN_TARGET})

find_library(MINIZIP_LIBRARY minizip)
include(FindZLIB)

if(ZLIB_FOUND AND MINIZIP_LIBRARY)
  message(STATUS "Using minizip")
  set(MINIZIP_LIBS ${MINIZIP_LIBRARY} ${ZLIB_LIBRARIES})
  add_definitions(-DHAVE_MINIZIP)
else()
  if(ENABLE_CODECS)
    if (ZLIB_FOUND)
      message(FATAL_ERROR "minizip required in this configuration")
    else()
      message(FATAL_ERROR "zlib required in this configuration")
    endif()
  endif()
endif()

target_link_libraries(${MAIN_TARGET}
  shared
  qhttp
  qslog
  ${MPV_LIBRARY}
  ${OPENGL_LIBS}
  ${QT_LIBRARIES}
  ${OS_LIBS}
  ${EXTRA_LIBS}
  ${X11_LIBRARIES}
  ${X11_Xrandr_LIB}
  ${BREAKPAD_LIBRARIES}
  ${ICU_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${RPI_LIBS}
  ${MINIZIP_LIBS}
)

install(TARGETS ${MAIN_TARGET} DESTINATION ${INSTALL_BIN_DIR})

dumpsyms(${MAIN_TARGET} ${CMAKE_BINARY_DIR}/${MAIN_TARGET}.symbols)
