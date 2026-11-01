#ifndef XRPROTOCOL_H
#define XRPROTOCOL_H

#include <stdint.h>

namespace XrProtocol
{
  const uint32_t kMagic = 0x58504d50;
  const uint32_t kVersion = 4;
  const int kSocketFd = 3;
  const int kReasonLength = 96;

  enum Type : uint32_t
  {
    Panel = 1,
    Stop = 2,
    Stereo = 3,
    Flat = 4,
    Failed = 5
  };

  enum Packing : uint32_t
  {
    Mono = 0,
    SideBySide = 1,
    OverUnder = 2
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
