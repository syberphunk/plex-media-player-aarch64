install (FILES ${CMAKE_SOURCE_DIR}/resources/desktop/plexmediaplayer.desktop DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/applications)
install (FILES ${CMAKE_SOURCE_DIR}/resources/images/icon.svg DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/icons/hicolor/scalable/apps RENAME plexmediaplayer.svg)

foreach(ICON_SIZE 16 32 48 64 128 256 512)
  install (FILES ${CMAKE_SOURCE_DIR}/resources/images/icons/icon-${ICON_SIZE}.png
           DESTINATION ${CMAKE_INSTALL_FULL_DATAROOTDIR}/icons/hicolor/${ICON_SIZE}x${ICON_SIZE}/apps
           RENAME plexmediaplayer.png)
endforeach()
