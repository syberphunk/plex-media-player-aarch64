#ifndef PMP_XR_HOST_ROOTPROPERTY_H
#define PMP_XR_HOST_ROOTPROPERTY_H

#include <stdint.h>

#include <string>

namespace RootProperty
{
  struct Reading
  {
    bool reached = false;
    bool present = false;
    uint32_t value = 0;
    std::string trouble;
  };

  Reading read(const char* name);
  Reading write(const char* name, uint32_t value);
  Reading remove(const char* name);
}

#endif
