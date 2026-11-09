#include "RootProperty.h"

#ifdef PMP_XR_HOST_X11

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstdio>

namespace
{
  enum Step
  {
    Look = 0,
    Put = 1,
    Take = 2
  };

  int g_refused = 0;

  int onRefused(Display*, XErrorEvent* event)
  {
    g_refused = event ? event->error_code : -1;
    return 0;
  }

  RootProperty::Reading reach(const char* name, Step step, uint32_t value)
  {
    RootProperty::Reading reading;

    Display* display = XOpenDisplay(nullptr);
    if (!display)
    {
      reading.trouble = "the X display would not open";
      return reading;
    }

    g_refused = 0;
    XErrorHandler before = XSetErrorHandler(onRefused);

    const Window root = DefaultRootWindow(display);
    const Atom atom = XInternAtom(display, name, False);

    if (step == Put)
    {
      unsigned long item = value;
      XChangeProperty(display, root, atom, XA_CARDINAL, 32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(&item), 1);
    }
    else if (step == Take)
      XDeleteProperty(display, root, atom);

    XSync(display, False);

    Atom actual = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long left = 0;
    unsigned char* data = nullptr;

    const int got = XGetWindowProperty(display, root, atom, 0L, 1L, False, XA_CARDINAL, &actual,
                                       &format, &count, &left, &data);
    if (got == 0 && data)
    {
      if (count >= 1 && format == 32)
      {
        reading.present = true;
        reading.value = static_cast<uint32_t>(*reinterpret_cast<unsigned long*>(data));
      }
      XFree(data);
    }

    XSync(display, False);
    XSetErrorHandler(before);
    XCloseDisplay(display);

    if (g_refused != 0)
    {
      char text[64];
      std::snprintf(text, sizeof(text), "the X server refused with error %d", g_refused);
      reading.trouble = text;
      return reading;
    }

    reading.reached = true;
    return reading;
  }
}

namespace RootProperty
{
  Reading read(const char* name)
  {
    return reach(name, Look, 0);
  }

  Reading write(const char* name, uint32_t value)
  {
    return reach(name, Put, value);
  }

  Reading remove(const char* name)
  {
    return reach(name, Take, 0);
  }
}

#else

namespace RootProperty
{
  Reading read(const char*)
  {
    Reading reading;
    reading.trouble = "this build has no X11";
    return reading;
  }

  Reading write(const char*, uint32_t)
  {
    return read(nullptr);
  }

  Reading remove(const char*)
  {
    return read(nullptr);
  }
}

#endif
