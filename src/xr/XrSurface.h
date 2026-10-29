#ifndef XRSURFACE_H
#define XRSURFACE_H

#include <QSize>
#include <QString>

#include <stdint.h>

#include "XrProtocol.h"

class XrSurface
{
public:
  XrSurface();
  ~XrSurface();

  bool begin(int socket, const QSize& size);
  void end();

  bool ready() const { return m_ready; }
  const QSize& size() const { return m_size; }

  int acquire();
  uint32_t framebuffer(int index) const;
  void submit(int index, uint32_t packing, bool swapped, float eyeAspect);
  void setVisible(bool visible);
  void place(const float rect[4], float reveal, float windowAspect);
  bool poll();

  const QString& failure() const { return m_failure; }
  bool impossible() const { return m_impossible; }

private:
  struct Buffer
  {
    void* bo = nullptr;
    void* image = nullptr;
    uint32_t texture = 0;
    uint32_t framebuffer = 0;
    int fd = -1;
    bool busy = false;
  };

  bool openDevice();
  bool createBuffers(bool withModifier);
  bool sendBuffers();
  void destroyBuffers();

  int m_socket = -1;
  int m_device = -1;
  void* m_gbm = nullptr;
  void* m_display = nullptr;
  bool m_ready = false;
  bool m_visible = true;
  float m_rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
  float m_windowAspect = 0.0f;
  float m_reveal = -1.0f;
  QSize m_size;
  uint32_t m_stride = 0;
  uint32_t m_offset = 0;
  uint64_t m_modifier = 0;
  Buffer m_buffers[XrProtocol::kBufferCount];
  QString m_failure;
  bool m_impossible = false;
};

#endif
