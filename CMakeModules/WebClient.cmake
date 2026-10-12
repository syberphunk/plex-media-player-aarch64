include(FetchDependencies)

option(WEB_CLIENT_TV_OLD "" OFF)
option(WEB_CLIENT_DISABLE_DESKTOP "" OFF)
option(WEB_CLIENT_PROVIDED_AT_RUNTIME "The web client is supplied on the target at install time" OFF)

if(WEB_CLIENT_PROVIDED_AT_RUNTIME)
  set(WEB_TV_DIR "")
  set(WEB_DESKTOP_DIR "")
  message(STATUS "web-client: supplied at install time")
  return()
endif()

# This is the line to edit when you bump the web-client.
set(WEB_CLIENT_BUILD_ID 183-045db5be50e175)

set(WEB_CLIENT_LOCAL_DIR "" CACHE PATH "Local directory with the web-client archives (offline build)")

set(WEB_CLIENT_UNPACKED_DIR "" CACHE PATH "Local directory with an already-unpacked web client")

function(extract_local_web_client _name _version _outvar)
  set(_archive "${WEB_CLIENT_LOCAL_DIR}/${_name}-${_version}.tar.xz")
  if(NOT EXISTS "${_archive}")
    message(FATAL_ERROR
      "Web client archive not found: ${_archive}\n"
      "Run scripts/fetch-web-client.sh to populate ${WEB_CLIENT_LOCAL_DIR}.")
  endif()

  if(EXISTS "${_archive}.sha1")
    file(STRINGS "${_archive}.sha1" _expected LIMIT_COUNT 1)
    string(SUBSTRING "${_expected}" 0 40 _expected)
    file(SHA1 "${_archive}" _actual)
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR
        "Checksum mismatch for ${_archive}\n"
        "  expected: ${_expected}\n  actual:   ${_actual}")
    endif()
    message(STATUS "Verified ${_name}-${_version}.tar.xz (sha1 ${_actual})")
  else()
    message(WARNING "No .sha1 sidecar for ${_archive}; skipping integrity check")
  endif()

  set(_dest "${CMAKE_BINARY_DIR}/web-client")
  set(_dir "${_dest}/${_name}-${_version}")
  if(NOT EXISTS "${_dir}/_FINISHED")
    message(STATUS "Unpacking ${_name}-${_version}.tar.xz...")
    file(REMOVE_RECURSE "${_dir}")
    file(MAKE_DIRECTORY "${_dest}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xJf "${_archive}"
      WORKING_DIRECTORY "${_dest}"
      RESULT_VARIABLE _res
    )
    if(NOT _res EQUAL 0)
      message(FATAL_ERROR "Failed to unpack ${_archive}")
    endif()
    if(NOT EXISTS "${_dir}/index.html")
      message(FATAL_ERROR "${_archive} did not unpack to ${_dir} (no index.html)")
    endif()
    file(WRITE "${_dir}/_FINISHED" "")
  endif()
  set(${_outvar} "${_dir}" PARENT_SCOPE)
endfunction()

if(WEB_CLIENT_UNPACKED_DIR AND IS_DIRECTORY "${WEB_CLIENT_UNPACKED_DIR}")
  foreach(_variant tv desktop)
    if(NOT EXISTS "${WEB_CLIENT_UNPACKED_DIR}/${_variant}/index.html")
      message(FATAL_ERROR
        "${WEB_CLIENT_UNPACKED_DIR}/${_variant}/index.html is missing.\n"
        "WEB_CLIENT_UNPACKED_DIR must hold desktop/ and tv/ subdirectories; "
        "scripts/extract-snap-web-client.sh produces that layout.")
    endif()
  endforeach()

  set(WEB_TV_DIR "${WEB_CLIENT_UNPACKED_DIR}/tv")
  set(WEB_DESKTOP_DIR "${WEB_CLIENT_UNPACKED_DIR}/desktop")

  if(EXISTS "${WEB_CLIENT_UNPACKED_DIR}/buildid.cmake")
    include("${WEB_CLIENT_UNPACKED_DIR}/buildid.cmake")
  endif()
  message(STATUS "web-client tv:      ${WEB_TV_DIR}")
  message(STATUS "web-client desktop: ${WEB_DESKTOP_DIR}")
  return()
endif()

if(WEB_CLIENT_LOCAL_DIR AND IS_DIRECTORY "${WEB_CLIENT_LOCAL_DIR}")
  message(STATUS "Using local web-client from ${WEB_CLIENT_LOCAL_DIR}")
  include("${WEB_CLIENT_LOCAL_DIR}/buildid.cmake")
else()
  get_content_of_url(
    URL "https://artifacts.plex.tv/web-client-pmp/${WEB_CLIENT_BUILD_ID}/buildid.cmake"
    CONTENT_VAR BUILDIDS
    FILENAME "buildid-${WEB_CLIENT_BUILD_ID}.cmake"
  )
  include("${DEPENDENCY_CACHE_DIR}/buildid-${WEB_CLIENT_BUILD_ID}.cmake")
endif()

if(WEB_CLIENT_TV_OLD)
  set(TV_VERSION ${TVOLD_VERSION})
endif()

if(WEB_CLIENT_LOCAL_DIR AND IS_DIRECTORY "${WEB_CLIENT_LOCAL_DIR}")
  extract_local_web_client("web-client-tv" "${TV_VERSION}" WEB_TV_DIR)
  if(NOT WEB_CLIENT_DISABLE_DESKTOP)
    extract_local_web_client("web-client-desktop" "${DESKTOP_VERSION}" WEB_DESKTOP_DIR)
  endif()
else()
  if(NOT WEB_CLIENT_DISABLE_DESKTOP)
    download_deps("web-client-desktop"
                  BUILD_NUMBER ${WEB_CLIENT_BUILD_ID}
                  VARIANT ${DESKTOP_VERSION}
                  NO_HASH_FILE
                  ARCHSTR "universal"
                  BASE_URL "https://artifacts.plex.tv/web-client-pmp/${WEB_CLIENT_BUILD_ID}"
                  DIRECTORY WEB_DESKTOP_DIR
    )
  endif()

  download_deps("web-client-tv"
                BUILD_NUMBER ${WEB_CLIENT_BUILD_ID}
                VARIANT ${TV_VERSION}
                NO_HASH_FILE
                ARCHSTR "universal"
                BASE_URL "https://artifacts.plex.tv/web-client-pmp/${WEB_CLIENT_BUILD_ID}"
                DIRECTORY WEB_TV_DIR
  )
endif()

message(STATUS "web-client tv:      ${WEB_TV_DIR}")
if(NOT WEB_CLIENT_DISABLE_DESKTOP)
  message(STATUS "web-client desktop: ${WEB_DESKTOP_DIR}")
endif()
