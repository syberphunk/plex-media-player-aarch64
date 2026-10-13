set(MAIN_TARGET PlexMediaPlayer)

# Name of the output binary, defaults are only used on Linux
set(MAIN_NAME plexmediaplayer)

if(APPLE)
  set(MAIN_NAME "Plex Media Player")
elseif(WIN32)
  set(MAIN_NAME "PlexMediaPlayer")
endif(APPLE)

configure_file(src/shared/Names.cpp.in src/shared/Names.cpp @ONLY)