include_directories(${PROJECT_SOURCE_DIR}/src)

if(ENABLE_CRASHDUMP)
  set(CRASHDUMP_SRCS CrashUploader.cpp CrashUploader.h)
  add_definitions(-DENABLE_CRASHDUMP -DCRASHDUMP_SECRET="${CRASHDUMP_SECRET}")
endif(ENABLE_CRASHDUMP)

add_executable(${HELPER_TARGET}
  ${CRASHDUMP_SRCS}
  HelperMain.cpp
  HelperSocket.cpp
  HelperSocket.h
)

target_link_libraries(${HELPER_TARGET} Qt6::Core Qt6::Network qslog shared)
std_target_properties(${HELPER_TARGET})
set_target_properties(${HELPER_TARGET} PROPERTIES
  INSTALL_RPATH "${QTROOT}/lib"
  OUTPUT_NAME ${HELPER_NAME}
)

add_custom_command(
  TARGET ${HELPER_TARGET}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${HELPER_TARGET}> $<TARGET_FILE_DIR:${MAIN_TARGET}>
)

install(TARGETS ${HELPER_TARGET} DESTINATION ${INSTALL_BIN_DIR})

clang_tidy(${HELPER_TARGET})
