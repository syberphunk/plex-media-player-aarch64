#ifndef XRPROTOCOL_H
#define XRPROTOCOL_H

#include <stdint.h>

namespace XrProtocol
{
  const uint32_t kMagic = 0x58504d50;
  const uint32_t kVersion = 8;
  const int kSocketFd = 3;
  const int kReasonLength = 96;

  enum Type : uint32_t
  {
    Panel = 1,
    Stop = 2,
    Stereo = 3,
    Flat = 4,
    Failed = 5,
    Pointer = 6,
    Mark = 7
  };

  enum Packing : uint32_t
  {
    Mono = 0,
    SideBySide = 1,
    OverUnder = 2
  };

  enum Aim : uint32_t
  {
    Unaimed = 0,
    DashboardDevice = 1,
    AimedController = 2,
    PointedController = 3
  };

  struct Message
  {
    uint32_t magic;
    uint32_t version;
    uint32_t type;

    uint32_t packing;
    uint32_t swapped;
    uint32_t flags;
    uint32_t appid;

    uint32_t mouseWide;
    uint32_t mouseTall;

    uint32_t windowWide;
    uint32_t windowTall;

    uint32_t panelMillimetres;
    uint32_t texelAspectThousandths;
    uint32_t pointerWideThousandths;

    uint32_t curveRadiusMillimetres;
    uint32_t curveSweepThousandths;
    uint32_t curveTallMillimetres;
    uint32_t aimedBy;
    int32_t aimMillimetres[3];

    uint32_t markAcrossMillionths;
    uint32_t markDownMillionths;
    uint32_t markSay;

    char reason[kReasonLength];
  };

  inline Message make(Type type)
  {
    Message message = {};
    message.magic = kMagic;
    message.version = kVersion;
    message.type = type;
    return message;
  }

  inline bool valid(const Message& message)
  {
    return message.magic == kMagic && message.version == kVersion;
  }
}

#endif
