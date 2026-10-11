include(FetchDependencies)

if(WIN32)
  set(WINARCHSTR ARCHSTR windows-x86_64)
endif(WIN32)

if(NOT IS_DIRECTORY ${QTROOT})
  download_deps(
		"plexmediaplayer-qt"
		DIRECTORY dir
		DEPHASH_VAR QT_DEPS_HASH
    BUILD_NUMBER 206
    ARTIFACTNAME konvergo-qt
    DYLIB_SCRIPT_PATH ${PROJECT_SOURCE_DIR}/scripts/fix-install-names.py
	)
  set(QTROOT ${dir})

  # Write qt.conf in the Qt depends directory so that the Qt tools can find QML files
  set(QTCONFCONTENT "[Paths]
    Prefix=${QTROOT}
    ")
  file(WRITE ${QTROOT}/bin/qt.conf ${QTCONFCONTENT})
endif()

message(STATUS "Qt root directory: ${QTROOT}")

list(APPEND CMAKE_FIND_ROOT_PATH ${QTROOT})
list(APPEND CMAKE_PREFIX_PATH ${QTROOT})

set(REQUIRED_QT_VERSION "6.5.0")

set(components Core Gui Network WebChannel Qml Quick QuickControls2 Xml WebEngineQuick Widgets OpenGL)

if(LINUX_DBUS)
  set(components ${components} DBus)
endif(LINUX_DBUS)

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS ${components})

foreach(COMP ${components})
	list(APPEND QT_LIBRARIES Qt6::${COMP})
	include_directories(${Qt6${COMP}_INCLUDE_DIRS})
endforeach(COMP ${components})

message(STATUS "Qt version: ${Qt6Core_VERSION}")

if(UNIX AND (NOT APPLE) AND ((NOT BUILD_TARGET STREQUAL "RPI")))
  find_package(X11)
  if(X11_FOUND)
    add_definitions(-DUSE_X11EXTRAS)
    include_directories(${X11_INCLUDE_DIR})
    list(APPEND QT_LIBRARIES ${X11_LIBRARIES})
  else()
    message(STATUS "X11 not found, building without X11 integration")
  endif()
endif()
