#include "XrSurface.h"

#ifdef HAVE_XR_DMABUF

#include <QDir>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>

#if __has_include(<drm_fourcc.h>)
#include <drm_fourcc.h>
#endif

#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 0ULL
#endif
#ifndef DRM_FORMAT_MOD_INVALID
#define DRM_FORMAT_MOD_INVALID 0x00ffffffffffffffULL
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "QsLog.h"

namespace
{
  typedef EGLImageKHR (*CreateImageProc)(EGLDisplay, EGLContext, EGLenum, EGLClientBuffer,
                                         const EGLint*);
  typedef EGLBoolean (*DestroyImageProc)(EGLDisplay, EGLImageKHR);
  typedef void (*TargetTextureProc)(GLenum, EGLImageKHR);

  CreateImageProc g_createImage = nullptr;
  DestroyImageProc g_destroyImage = nullptr;
  TargetTextureProc g_targetTexture = nullptr;

  bool resolveExtensions()
  {
    if (g_createImage && g_destroyImage && g_targetTexture)
      return true;

    g_createImage = reinterpret_cast<CreateImageProc>(eglGetProcAddress("eglCreateImageKHR"));
    g_destroyImage = reinterpret_cast<DestroyImageProc>(eglGetProcAddress("eglDestroyImageKHR"));
    g_targetTexture =
      reinterpret_cast<TargetTextureProc>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    return g_createImage && g_destroyImage && g_targetTexture;
  }
}

XrSurface::XrSurface()
{
}

XrSurface::~XrSurface()
{
  end();
}

bool XrSurface::openDevice()
{
  const QStringList nodes = QDir(QStringLiteral("/dev/dri"))
                              .entryList(QStringList() << QStringLiteral("renderD*"),
                                         QDir::System | QDir::Files | QDir::NoDotAndDotDot,
                                         QDir::Name);

  for (const QString& node : nodes)
  {
    const QByteArray path = QStringLiteral("/dev/dri/%1").arg(node).toUtf8();
    const int fd = open(path.constData(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
      continue;

    struct gbm_device* device = gbm_create_device(fd);
    if (!device)
    {
      close(fd);
      continue;
    }

    m_device = fd;
    m_gbm = device;
    QLOG_INFO() << "Stereoscopic: render node" << qPrintable(QString::fromUtf8(path));
    return true;
  }

  m_failure = QStringLiteral("no usable render node in /dev/dri");
  return false;
}

bool XrSurface::createBuffers(bool withModifier)
{
  QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
  struct gbm_device* device = static_cast<struct gbm_device*>(m_gbm);
  EGLDisplay display = static_cast<EGLDisplay>(m_display);

  for (uint32_t i = 0; i < XrProtocol::kBufferCount; ++i)
  {
    Buffer& buffer = m_buffers[i];

    const uint64_t wanted = DRM_FORMAT_MOD_LINEAR;
    struct gbm_bo* bo =
      withModifier ? gbm_bo_create_with_modifiers(device, m_size.width(), m_size.height(),
                                                  GBM_FORMAT_ABGR8888, &wanted, 1)
                   : gbm_bo_create(device, m_size.width(), m_size.height(), GBM_FORMAT_ABGR8888,
                                   GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!bo)
    {
      m_failure = QStringLiteral("the driver would not allocate a %1x%2 linear buffer")
                    .arg(m_size.width())
                    .arg(m_size.height());
      return false;
    }

    buffer.bo = bo;
    buffer.fd = gbm_bo_get_fd(bo);
    if (buffer.fd < 0)
    {
      m_failure = QStringLiteral("the allocated buffer could not be exported");
      return false;
    }

    const uint32_t stride = gbm_bo_get_stride(bo);
    const uint32_t offset = gbm_bo_get_offset(bo, 0);
    const uint64_t modifier = gbm_bo_get_modifier(bo);

    if (i == 0)
    {
      m_stride = stride;
      m_offset = offset;
      m_modifier = modifier;

      if (withModifier && modifier == DRM_FORMAT_MOD_INVALID)
      {
        m_failure = QStringLiteral("the driver would not state the buffer's modifier");
        return false;
      }
    }
    else if (stride != m_stride || offset != m_offset || modifier != m_modifier)
    {
      m_failure = QStringLiteral("the driver returned buffers with differing layouts");
      return false;
    }

    const EGLint attributes[] = {EGL_WIDTH,
                                 m_size.width(),
                                 EGL_HEIGHT,
                                 m_size.height(),
                                 EGL_LINUX_DRM_FOURCC_EXT,
                                 static_cast<EGLint>(GBM_FORMAT_ABGR8888),
                                 EGL_DMA_BUF_PLANE0_FD_EXT,
                                 buffer.fd,
                                 EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                 static_cast<EGLint>(offset),
                                 EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                 static_cast<EGLint>(stride),
                                 EGL_NONE};

    EGLImageKHR image = g_createImage(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr,
                                      attributes);
    if (image == EGL_NO_IMAGE_KHR)
    {
      m_failure = QStringLiteral("EGL would not import the shared buffer (0x%1)")
                    .arg(eglGetError(), 0, 16);
      return false;
    }

    buffer.image = image;

    gl->glGenTextures(1, &buffer.texture);
    gl->glBindTexture(GL_TEXTURE_2D, buffer.texture);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_targetTexture(GL_TEXTURE_2D, image);

    gl->glGenFramebuffers(1, &buffer.framebuffer);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, buffer.framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               buffer.texture, 0);

    const GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
      m_failure = QStringLiteral("the shared buffer is not drawable (0x%1)").arg(status, 0, 16);
      gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
    }
  }

  gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
  gl->glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

bool XrSurface::sendBuffers()
{
  XrProtocol::Message message = XrProtocol::make(XrProtocol::Buffers);
  message.count = XrProtocol::kBufferCount;
  message.width = m_size.width();
  message.height = m_size.height();
  message.fourcc = GBM_FORMAT_ABGR8888;
  message.stride = m_stride;
  message.offset = m_offset;
  message.modifier = m_modifier;

  int fds[XrProtocol::kBufferCount];
  for (uint32_t i = 0; i < XrProtocol::kBufferCount; ++i)
    fds[i] = m_buffers[i].fd;

  struct iovec iov = {&message, sizeof(message)};
  char control[CMSG_SPACE(sizeof(fds))] = {0};

  struct msghdr header = {};
  header.msg_iov = &iov;
  header.msg_iovlen = 1;
  header.msg_control = control;
  header.msg_controllen = sizeof(control);

  struct cmsghdr* cmsg = CMSG_FIRSTHDR(&header);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
  memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));

  while (sendmsg(m_socket, &header, MSG_NOSIGNAL) < 0)
  {
    if (errno == EINTR)
      continue;
    m_failure = QStringLiteral("the buffers could not be handed over (%1)")
                  .arg(QString::fromUtf8(strerror(errno)));
    return false;
  }

  for (uint32_t i = 0; i < XrProtocol::kBufferCount; ++i)
  {
    close(m_buffers[i].fd);
    m_buffers[i].fd = -1;
  }

  return true;
}

bool XrSurface::begin(int socket, const QSize& size)
{
  if (m_ready)
    end();

  m_failure.clear();
  m_socket = socket;
  m_size = size;

  if (socket < 0 || size.width() < 2 || size.height() < 2)
  {
    m_failure = QStringLiteral("the video size is not usable yet");
    return false;
  }

  if (!QOpenGLContext::currentContext())
  {
    m_failure = QStringLiteral("there is no current OpenGL context");
    return false;
  }

  EGLDisplay display = eglGetCurrentDisplay();
  if (display == EGL_NO_DISPLAY)
  {
    m_failure = QStringLiteral("the window is not on EGL, so buffers cannot be shared");
    return false;
  }
  m_display = display;

  if (!resolveExtensions())
  {
    m_failure = QStringLiteral("this driver has no dmabuf import for EGL");
    return false;
  }

  if (!openDevice() || !createBuffers(true))
  {
    destroyBuffers();
    QLOG_INFO() << "Stereoscopic: a buffer with a stated modifier was not usable ("
                << qPrintable(m_failure) << "), asking for a plain linear one";
    m_failure.clear();
    if (!openDevice() || !createBuffers(false))
    {
      destroyBuffers();
      return false;
    }
  }

  if (!sendBuffers())
  {
    destroyBuffers();
    return false;
  }

  m_ready = true;
  m_visible = true;
  QLOG_INFO() << "Stereoscopic: sharing" << XrProtocol::kBufferCount << "buffers of"
              << size.width() << "x" << size.height() << "stride" << m_stride << "modifier"
              << QString::number(m_modifier, 16);
  return true;
}

void XrSurface::destroyBuffers()
{
  QOpenGLContext* context = QOpenGLContext::currentContext();
  QOpenGLFunctions* gl = context ? context->functions() : nullptr;

  for (Buffer& buffer : m_buffers)
  {
    if (gl && buffer.framebuffer)
      gl->glDeleteFramebuffers(1, &buffer.framebuffer);
    if (gl && buffer.texture)
      gl->glDeleteTextures(1, &buffer.texture);
    if (buffer.image && m_display && g_destroyImage)
      g_destroyImage(static_cast<EGLDisplay>(m_display), buffer.image);
    if (buffer.fd >= 0)
      close(buffer.fd);
    if (buffer.bo)
      gbm_bo_destroy(static_cast<struct gbm_bo*>(buffer.bo));
    buffer = Buffer();
  }

  if (m_gbm)
  {
    gbm_device_destroy(static_cast<struct gbm_device*>(m_gbm));
    m_gbm = nullptr;
  }
  if (m_device >= 0)
  {
    close(m_device);
    m_device = -1;
  }
}

void XrSurface::end()
{
  destroyBuffers();
  m_display = nullptr;
  m_socket = -1;
  m_ready = false;
  m_size = QSize();
}

int XrSurface::acquire()
{
  if (!m_ready)
    return -1;

  for (uint32_t i = 0; i < XrProtocol::kBufferCount; ++i)
  {
    if (!m_buffers[i].busy)
      return static_cast<int>(i);
  }
  return -1;
}

uint32_t XrSurface::framebuffer(int index) const
{
  if (index < 0 || index >= static_cast<int>(XrProtocol::kBufferCount))
    return 0;
  return m_buffers[index].framebuffer;
}

void XrSurface::submit(int index, uint32_t packing, bool swapped, float eyeAspect)
{
  if (!m_ready || index < 0 || index >= static_cast<int>(XrProtocol::kBufferCount))
    return;

  XrProtocol::Message message = XrProtocol::make(XrProtocol::Frame);
  message.index = static_cast<uint32_t>(index);
  message.packing = packing;
  message.swapped = swapped ? 1 : 0;
  message.eyeAspect = eyeAspect;

  m_buffers[index].busy = true;

  if (send(m_socket, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) < 0)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      m_buffers[index].busy = false;
      return;
    }
    m_failure = QStringLiteral("the presenter connection broke (%1)")
                  .arg(QString::fromUtf8(strerror(errno)));
    m_ready = false;
  }
}

void XrSurface::setVisible(bool visible)
{
  if (!m_ready || visible == m_visible)
    return;

  XrProtocol::Message message =
    XrProtocol::make(visible ? XrProtocol::Show : XrProtocol::Hide);

  if (send(m_socket, &message, sizeof(message), MSG_NOSIGNAL | MSG_DONTWAIT) < 0)
    return;

  m_visible = visible;
}

bool XrSurface::poll()
{
  if (m_socket < 0)
    return m_ready;

  while (true)
  {
    XrProtocol::Message message{};
    const ssize_t got = recv(m_socket, &message, sizeof(message), MSG_DONTWAIT);

    if (got == 0)
    {
      m_failure = QStringLiteral("the presenter stopped");
      m_ready = false;
      return false;
    }
    if (got < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return m_ready;
      if (errno == EINTR)
        continue;
      m_failure = QStringLiteral("the presenter connection failed (%1)")
                    .arg(QString::fromUtf8(strerror(errno)));
      m_ready = false;
      return false;
    }

    if (got != static_cast<ssize_t>(sizeof(message)) || !XrProtocol::valid(message))
      continue;

    if (message.type == XrProtocol::Release && message.index < XrProtocol::kBufferCount)
      m_buffers[message.index].busy = false;
    else if (message.type == XrProtocol::Failed)
    {
      m_failure = QStringLiteral("the presenter could not use the shared buffers");
      m_ready = false;
      return false;
    }
  }
}

#else

XrSurface::XrSurface()
{
}

XrSurface::~XrSurface()
{
}

bool XrSurface::begin(int, const QSize&)
{
  m_failure = QStringLiteral("this build has no gbm/EGL buffer sharing");
  return false;
}

void XrSurface::end()
{
}

int XrSurface::acquire()
{
  return -1;
}

uint32_t XrSurface::framebuffer(int) const
{
  return 0;
}

void XrSurface::submit(int, uint32_t, bool, float)
{
}

void XrSurface::setVisible(bool)
{
}


bool XrSurface::poll()
{
  return false;
}

#endif
