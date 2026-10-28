#ifndef XRPROTOCOL_H
#define XRPROTOCOL_H

#include <stdint.h>

namespace XrProtocol
{
  const uint32_t kMagic = 0x58504d50;
  const uint32_t kVersion = 1;
  const uint32_t kBufferCount = 3;
  const int kSocketFd = 3;

  enum Type : uint32_t
  {
    Buffers = 1,
    Frame = 2,
    Release = 3,
    Started = 4,
    Recentre = 5,
    Stop = 6,
    Failed = 7
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

    uint32_t count;
    uint32_t width;
    uint32_t height;
    uint32_t fourcc;
    uint32_t stride;
    uint32_t offset;
    uint32_t pad;
    uint64_t modifier;

    uint32_t index;
    uint32_t packing;
    uint32_t swapped;
    float eyeAspect;
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
