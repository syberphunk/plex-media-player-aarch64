#include "PlayerQuickItem.h"

#include <QOpenGLContext>
#include <QPointF>
#include <QSizeF>

#include <QtOpenGL/QOpenGLFramebufferObject>

#include <QtQuick/QQuickWindow>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QVector>
#include <QStringList>

#include <mpv/render_gl.h>

#include "QsLog.h"
#include "utils/Utils.h"
#include "xr/XrPresenter.h"
#include "xr/XrProtocol.h"

#ifdef USE_X11EXTRAS
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#endif

namespace
{
  const float kControlsReveal = 0.18f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void* get_proc_address(void* ctx, const char* name)
{
  Q_UNUSED(ctx);

  QOpenGLContext* glctx = QOpenGLContext::currentContext();
  if (!glctx)
    return nullptr;

  void *res = (void *)glctx->getProcAddress(QByteArray(name));
  return res;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerRenderer::PlayerRenderer(PlayerQuickItem* item)
: m_item(item), m_mpv(item->m_mpv), m_mpvGL(nullptr), m_initialized(false),
  m_playbackActive(false), m_stereoWanted(false), m_stereoPacking(0), m_stereoSwapped(false),
  m_stereoControls(false), m_windowShot(nullptr), m_videoFbo(nullptr), m_shotReports(0),
  m_probeTarget(nullptr), m_probeWanted(false), m_probeRunning(false), m_probeTick(0)
{
  m_item->m_renderer = this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerRenderer::~PlayerRenderer()
{
  if (m_item->m_renderer == this)
    m_item->m_renderer = nullptr;

  delete m_probeTarget;
  m_probeTarget = nullptr;
  delete m_windowShot;
  m_windowShot = nullptr;
  delete m_videoFbo;
  m_videoFbo = nullptr;

  // Keep in mind that the m_mpv handle must be held until this is done.
  if (m_mpvGL)
  {
    mpv_render_context_set_update_callback(m_mpvGL, nullptr, nullptr);
    mpv_render_context_free(m_mpvGL);
  }
  m_mpvGL = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool PlayerRenderer::init()
{
  mpv_opengl_init_params opengl_params = {
      .get_proc_address = get_proc_address,
      .get_proc_address_ctx = NULL,
  };

  QVector<mpv_render_param> params;
  params.append({MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL});
  params.append({MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &opengl_params});

#ifdef USE_X11EXTRAS
  auto* x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
  if (x11App && x11App->display())
    params.append({MPV_RENDER_PARAM_X11_DISPLAY, x11App->display()});
  else
    QLOG_INFO() << "Not an X11 platform plugin, not passing an X11 display to mpv";
#endif

  params.append({MPV_RENDER_PARAM_INVALID, nullptr});
  int err = mpv_render_context_create(&m_mpvGL, m_mpv, params.data());

  if (err >= 0) {
    mpv_render_context_set_update_callback(m_mpvGL, PlayerQuickItem::on_update, (void *)m_item);
    return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::collectGraphicsInfo()
{
  QOpenGLContext* glctx = QOpenGLContext::currentContext();
  if (!glctx || !glctx->isValid())
  {
    QLOG_ERROR() << "No valid OpenGL context at scenegraph init";
    return;
  }

  QStringList logged;
  QString info = "\nOpenGL:\n";
  int syms[4] = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION};
  const char* names[4] = {"vendor", "renderer", "version", "glsl"};
  for (int i = 0; i < 4; i++)
  {
    auto s = (char *)glctx->functions()->glGetString(syms[i]);
    if (s)
    {
      info += QString("  ") + QString::fromUtf8(s) + "\n";
      logged << QString("%1=%2").arg(names[i], QString::fromUtf8(s));
    }
  }
  info += "\n";
  m_graphicsInfo = info;

  QLOG_INFO() << "OpenGL:" << qPrintable(logged.join(" "));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::syncStereo()
{
  if (!XrPresenter::exists())
  {
    m_stereoWanted = false;
    m_probeWanted = false;
    return;
  }

  XrPresenter& presenter = XrPresenter::Get();

  m_probeWanted = presenter.probeWanted();
  m_probeSize = presenter.probeSize();
  if (m_probeWanted && !m_probeRunning)
  {
    m_probe.reset();
    m_probeTick = 0;
  }
  m_probeRunning = m_probeWanted;

  const bool wanted = presenter.wanted() && presenter.panelStereo();
  if (wanted != m_stereoWanted)
    m_shotReports = 0;

  m_stereoWanted = wanted;
  m_packedSize = presenter.videoSize();
  m_stereoPacking = presenter.packing();
  m_stereoSwapped = presenter.swapped();
  m_stereoControls = presenter.controlsActive();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::probePicture(QOpenGLFramebufferObject* fbo)
{
  if (!m_probeWanted || !m_playbackActive || !fbo || !fbo->isValid())
    return;
  if (m_probeSize.width() < 2 || m_probeSize.height() < 2)
    return;
  if (m_probe.decided())
    return;
  if (++m_probeTick % 8 != 0)
    return;

  QOpenGLContext* context = QOpenGLContext::currentContext();
  if (!context)
    return;

  if (!m_probeTarget)
    m_probeTarget = new QOpenGLFramebufferObject(StereoProbe::kGridWidth,
                                                 StereoProbe::kGridHeight);
  if (!m_probeTarget->isValid())
    return;

  const double aspect = double(m_probeSize.width()) / double(m_probeSize.height());
  int pictureWidth = fbo->width();
  int pictureHeight = fbo->height();
  if (double(pictureWidth) / double(pictureHeight) > aspect)
    pictureWidth = int(qRound(pictureHeight * aspect));
  else
    pictureHeight = int(qRound(pictureWidth / aspect));

  const int left = (fbo->width() - pictureWidth) / 2;
  const int bottom = (fbo->height() - pictureHeight) / 2;

  QOpenGLExtraFunctions* gl = context->extraFunctions();
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo->handle());
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_probeTarget->handle());
  gl->glBlitFramebuffer(left, bottom, left + pictureWidth, bottom + pictureHeight, 0, 0,
                        m_probeTarget->width(), m_probeTarget->height(), GL_COLOR_BUFFER_BIT,
                        GL_LINEAR);

  QVector<uint8_t> pixels(m_probeTarget->width() * m_probeTarget->height() * 4, 0);
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_probeTarget->handle());
  gl->glReadPixels(0, 0, m_probeTarget->width(), m_probeTarget->height(), GL_RGBA,
                   GL_UNSIGNED_BYTE, pixels.data());
  gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle());

  if (!m_probe.add(pixels.constData(), m_probeTarget->width(), m_probeTarget->height()))
    return;

  if (!m_probe.decided())
    return;

  uint32_t packing = XrProtocol::Mono;
  if (m_probe.verdict() == StereoProbe::Verdict::SideBySide)
    packing = XrProtocol::SideBySide;
  else if (m_probe.verdict() == StereoProbe::Verdict::OverUnder)
    packing = XrProtocol::OverUnder;

  QLOG_INFO() << "Stereoscopic: the picture reads as"
              << (packing == XrProtocol::SideBySide
                    ? "side by side"
                    : (packing == XrProtocol::OverUnder ? "over-under" : "flat"))
              << "-" << qPrintable(m_probe.reading());

  m_probeWanted = false;
  QMetaObject::invokeMethod(&XrPresenter::Get(), "reportProbe", Qt::QueuedConnection,
                            Q_ARG(int, int(packing)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::reportShot(QOpenGLExtraFunctions* gl)
{
  if (m_shotReports >= 3 || !m_windowShot)
    return;

  m_shotReports++;

  QOpenGLFramebufferObject sample(16, 16);
  if (!sample.isValid())
    return;

  const int w = m_windowShot->width();
  const int h = m_windowShot->height();

  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_windowShot->handle());
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sample.handle());
  gl->glBlitFramebuffer(0, 0, w, h, 0, 0, 16, 16, GL_COLOR_BUFFER_BIT, GL_LINEAR);

  QVector<uint8_t> pixels(16 * 16 * 4, 0);
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, sample.handle());
  gl->glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  double sum = 0.0;
  for (int i = 0; i < 16 * 16; ++i)
    sum += 0.2126 * pixels[i * 4] + 0.7152 * pixels[i * 4 + 1] + 0.0722 * pixels[i * 4 + 2];

  QLOG_INFO() << "Stereoscopic: squeezing the window" << w << "x" << h << "mean luminance"
              << qRound(sum / (16.0 * 16.0)) << "video rectangle" << m_windowVideoRect.x()
              << m_windowVideoRect.y() << m_windowVideoRect.width() << m_windowVideoRect.height()
              << "packed" << m_packedSize.width() << "x" << m_packedSize.height();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::overlayVideo(QOpenGLExtraFunctions* gl, int half)
{
  const int w = m_windowSize.width();
  const int h = m_windowSize.height();

  QRect rect = m_windowVideoRect.intersected(QRect(0, 0, w, h));
  if (rect.width() < 4 || rect.height() < 4)
    rect = QRect(0, 0, w, h);

  if (m_stereoControls)
  {
    const int across = int(rect.width() * (1.0f - kControlsReveal));
    const int down = int(rect.height() * (1.0f - kControlsReveal));
    rect = QRect(rect.x() + (rect.width() - across) / 2, rect.y(), across, down);
  }

  const int pw = m_videoFbo->width();
  const int ph = m_videoFbo->height();

  const int top = h - rect.y();
  const int bottom = top - rect.height();

  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_videoFbo->handle());
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

  for (int eye = 0; eye < 2; ++eye)
  {
    const int which = m_stereoSwapped ? 1 - eye : eye;

    int sx0 = 0;
    int sy0 = 0;
    int sx1 = pw;
    int sy1 = ph;

    if (m_stereoPacking == XrProtocol::SideBySide)
    {
      const int part = pw / 2;
      sx0 = which * part;
      sx1 = sx0 + part;
    }
    else if (m_stereoPacking == XrProtocol::OverUnder)
    {
      const int part = ph / 2;
      sy0 = (1 - which) * part;
      sy1 = sy0 + part;
    }

    const int dx0 = eye * half + rect.x() / 2;
    const int dx1 = dx0 + rect.width() / 2;

    gl->glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, bottom, dx1, top, GL_COLOR_BUFFER_BIT,
                          GL_LINEAR);
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::composeWindow()
{
  if (!m_stereoWanted)
    return;

  const int w = m_windowSize.width();
  const int h = m_windowSize.height();
  if (w < 4 || h < 4)
    return;

  QOpenGLContext* context = QOpenGLContext::currentContext();
  if (!context)
    return;

  if (m_windowShot && m_windowShot->size() != m_windowSize)
  {
    delete m_windowShot;
    m_windowShot = nullptr;
  }
  if (!m_windowShot)
    m_windowShot = new QOpenGLFramebufferObject(m_windowSize);
  if (!m_windowShot->isValid())
    return;

  QOpenGLExtraFunctions* gl = context->extraFunctions();
  gl->glDisable(GL_SCISSOR_TEST);

  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_windowShot->handle());
  gl->glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

  reportShot(gl);

  const int half = w / 2;

  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl->glClear(GL_COLOR_BUFFER_BIT);

  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_windowShot->handle());
  for (int eye = 0; eye < 2; ++eye)
    gl->glBlitFramebuffer(0, 0, w, h, eye * half, 0, eye * half + half, h, GL_COLOR_BUFFER_BIT,
                          GL_LINEAR);

  if (m_playbackActive && m_videoFbo && m_videoFbo->isValid())
    overlayVideo(gl, half);

  gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
bool PlayerRenderer::renderPacked(QOpenGLFramebufferObject* target)
{
  if (!m_stereoWanted || !m_mpvGL)
    return false;
  if (!target || !target->isValid())
    return false;
  if (m_packedSize.width() < 4 || m_packedSize.height() < 4)
    return false;
  if (m_windowSize.width() < 4 || m_windowSize.height() < 4)
    return false;

  QOpenGLContext* context = QOpenGLContext::currentContext();
  if (!context)
    return false;

  if (m_videoFbo && m_videoFbo->size() != m_packedSize)
  {
    delete m_videoFbo;
    m_videoFbo = nullptr;
  }
  if (!m_videoFbo)
    m_videoFbo = new QOpenGLFramebufferObject(m_packedSize);
  if (!m_videoFbo->isValid())
  {
    delete m_videoFbo;
    m_videoFbo = nullptr;
    return false;
  }

  mpv_opengl_fbo mpv_fbo = {
    .fbo = (int)m_videoFbo->handle(),
    .w = m_packedSize.width(),
    .h = m_packedSize.height(),
  };
  int mpv_flip = 0;
  mpv_render_param params[] = {
    {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
    {MPV_RENDER_PARAM_FLIP_Y, &mpv_flip},
    {MPV_RENDER_PARAM_INVALID}
  };
  mpv_render_context_render(m_mpvGL, params);

  probePicture(m_videoFbo);

  const int pw = m_videoFbo->width();
  const int ph = m_videoFbo->height();

  int sx0 = 0;
  int sy0 = 0;
  int sx1 = pw;
  int sy1 = ph;

  if (m_stereoPacking == XrProtocol::SideBySide)
  {
    const int part = pw / 2;
    sx0 = m_stereoSwapped ? part : 0;
    sx1 = sx0 + part;
  }
  else if (m_stereoPacking == XrProtocol::OverUnder)
  {
    const int part = ph / 2;
    sy0 = m_stereoSwapped ? 0 : part;
    sy1 = sy0 + part;
  }

  QOpenGLExtraFunctions* gl = context->extraFunctions();
  gl->glDisable(GL_SCISSOR_TEST);
  gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_videoFbo->handle());
  gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->handle());
  gl->glBlitFramebuffer(sx0, sy0, sx1, sy1, 0, 0, target->width(), target->height(),
                        GL_COLOR_BUFFER_BIT, GL_LINEAR);
  gl->glBindFramebuffer(GL_FRAMEBUFFER, target->handle());

  return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::render()
{
  if (!m_initialized && m_mpv)
  {
    m_initialized = true;
    collectGraphicsInfo();
    if (!init())
      emit m_item->onFatalError(QObject::tr("Could not initialize OpenGL."));
  }

  QOpenGLFramebufferObject* fbo = framebufferObject();
  if (!m_mpvGL || !fbo || !fbo->isValid())
    return;

  if (renderPacked(fbo))
    return;

  mpv_opengl_fbo mpv_fbo = {
    .fbo = (int)fbo->handle(),
    .w = fbo->width(),
    .h = fbo->height(),
  };
  int mpv_flip = 0;
  mpv_render_param params[] = {
    {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
    {MPV_RENDER_PARAM_FLIP_Y, &mpv_flip},
    {MPV_RENDER_PARAM_INVALID}
  };
  mpv_render_context_render(m_mpvGL, params);

  probePicture(fbo);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::reportSwap()
{
  if (m_mpvGL)
    mpv_render_context_report_swap(m_mpvGL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerRenderer::synchronize(QQuickFramebufferObject* item)
{
  auto* player = static_cast<PlayerQuickItem*>(item);

  if (!m_mpv)
    m_mpv = player->m_mpv;

  if (!m_graphicsInfo.isEmpty())
  {
    player->m_debugInfo = m_graphicsInfo;
    m_graphicsInfo.clear();
  }

  m_playbackActive = player->m_playbackActive;

  QQuickWindow* win = player->window();
  if (win)
  {
    const qreal dpr = win->effectiveDevicePixelRatio();
    m_windowSize = QSize(int(qRound(win->width() * dpr)), int(qRound(win->height() * dpr)));

    const QPointF at = player->position();
    m_windowVideoRect = QRect(int(qRound(at.x() * dpr)), int(qRound(at.y() * dpr)),
                              int(qRound(player->width() * dpr)),
                              int(qRound(player->height() * dpr)));
  }

  syncStereo();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerQuickItem::PlayerQuickItem(QQuickItem* parent)
: QQuickFramebufferObject(parent), m_playbackActive(false), m_renderer(nullptr)
{
  connect(this, &QQuickItem::windowChanged, this, &PlayerQuickItem::onWindowChanged, Qt::DirectConnection);
  connect(this, &PlayerQuickItem::onFatalError, this, &PlayerQuickItem::onHandleFatalError, Qt::QueuedConnection);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerQuickItem::~PlayerQuickItem()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////
QQuickFramebufferObject::Renderer* PlayerQuickItem::createRenderer() const
{
  return new PlayerRenderer(const_cast<PlayerQuickItem*>(this));
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::onWindowChanged(QQuickWindow* win)
{
  if (!win)
    return;

  win->setPersistentGraphics(true);
  win->setPersistentSceneGraph(true);

  connect(win, &QQuickWindow::frameSwapped, this, &PlayerQuickItem::onFrameSwapped, Qt::DirectConnection);
  connect(win, &QQuickWindow::afterRendering, this, &PlayerQuickItem::onAfterRendering, Qt::DirectConnection);
  connect(win, &QQuickWindow::widthChanged, this, &PlayerQuickItem::updateVideoGeometry);
  connect(win, &QQuickWindow::heightChanged, this, &PlayerQuickItem::updateVideoGeometry);

  updateVideoGeometry();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::onFrameSwapped()
{
  if (m_renderer)
    m_renderer->reportSwap();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::onAfterRendering()
{
  if (!m_renderer)
    return;

  QQuickWindow* win = window();
  if (!win)
    return;

  win->beginExternalCommands();
  m_renderer->composeWindow();
  win->endExternalCommands();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::onVideoPlaybackActive(bool active)
{
  m_playbackActive = active;
  update();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::updateVideoGeometry()
{
  QQuickWindow* win = window();
  if (!win)
    return;

  const qreal dpr = win->devicePixelRatio();
  const QRect rc = PlayerComponent::Get().videoRectangle();

  if (rc.width() > 0 && rc.height() > 0 && dpr > 0)
  {
    setPosition(QPointF(rc.x() / dpr, rc.y() / dpr));
    setSize(QSizeF(rc.width() / dpr, rc.height() / dpr));
  }
  else
  {
    setPosition(QPointF(0, 0));
    setSize(QSizeF(win->width(), win->height()));
  }

  update();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::onHandleFatalError(QString message)
{
  throw FatalException(message);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::on_update(void *ctx)
{
  auto* self = static_cast<PlayerQuickItem*>(ctx);
  QMetaObject::invokeMethod(self, "update", Qt::QueuedConnection);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void PlayerQuickItem::initMpv(PlayerComponent* player)
{
  m_mpv = player->getMpvHandle();

  connect(player, &PlayerComponent::videoPlaybackActive, this, &PlayerQuickItem::onVideoPlaybackActive);
  connect(player, &PlayerComponent::onVideoRecangleChanged, this, &PlayerQuickItem::updateVideoGeometry);

  updateVideoGeometry();
  update();
}
