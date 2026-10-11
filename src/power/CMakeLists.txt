add_sources(PowerComponent.h PowerComponent.cpp)

if(LINUX_X11POWER)
  add_sources(PowerComponentX11.cpp PowerComponentX11.h)
endif(LINUX_X11POWER)

if(LINUX_DBUS)
  add_sources(PowerComponentDBus.cpp PowerComponentDBus.h)
endif(LINUX_DBUS)
