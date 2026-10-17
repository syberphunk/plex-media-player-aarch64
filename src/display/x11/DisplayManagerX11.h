#ifndef DISPLAYMANAGERX11_H_
#define DISPLAYMANAGERX11_H_

#include <qmetatype.h>

#include <X11/extensions/Xrandr.h>

// X11 headers are messing up things for us
#undef CursorShape
#undef Bool
#undef Status
#undef None
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef Expose

#include "display/DisplayManager.h"

class DisplayManagerX11 : public DisplayManager
{
  Q_OBJECT
private:
  Display* xdisplay;
  XRRScreenResources* resources;

public:
  DisplayManagerX11(QObject* parent) : DisplayManager(parent), xdisplay(0), resources(0) {};
  virtual ~DisplayManagerX11();

  bool initialize() override;
  bool setDisplayMode(int display, int mode) override;
  int getCurrentDisplayMode(int display) override;
  int getMainDisplay() override;
  int getDisplayFromPoint(int x, int y) override;
};

#endif /* DISPLAYMANAGERX11_H_ */
