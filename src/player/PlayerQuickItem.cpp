#include "PlayerQuickItem.h"

#include <QOpenGLContext>
#include <QPointF>
#include <QSizeF>

#include <QtOpenGL/QOpenGLFramebufferObject>

#include <QtQuick/QQuickWindow>
#include <QOpenGLFunctions>
#include <QVector>
#include <QStringList>

#include <mpv/render_gl.h>

#include "QsLog.h"
#include "utils/Utils.h"

#if defined(Q_OS_WIN32)
#include <windows.h>
#include <dwmapi.h>
#include <avrt.h>
#endif

#ifdef USE_X11EXTRAS
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
static void* get_proc_address(void* ctx, const char* name)
{
  Q_UNUSED(ctx);

  QOpenGLContext* glctx = QOpenGLContext::currentContext();
  if (!glctx)
    return nullptr;

  void *res = (void *)glctx->getProcAddress(QByteArray(name));
#ifdef Q_OS_WIN32
  // wglGetProcAddress(), which is used by Qt, does not always resolve all
  // builtin functions with all drivers (only extensions). Qt compensates this
  // for a degree, but does this only for functions Qt happens to need. So
  // we need our own falback as well.
  if (!res)
  {
    HMODULE handle = (HMODULE)QOpenGLContext::openGLModuleHandle();
    if (handle)
      res = (void *)GetProcAddress(handle, name);
  }
#endif
  return res;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerRenderer::PlayerRenderer(PlayerQuickItem* item)
: m_item(item), m_mpv(item->m_mpv), m_mpvGL(nullptr), m_initialized(false),
  m_playbackActive(false), m_hAvrtHandle(nullptr)
{
  m_item->m_renderer = this;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
PlayerRenderer::~PlayerRenderer()
{
  if (m_item->m_renderer == this)
    m_item->m_renderer = nullptr;

  setThreadCharacteristics(false);

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
#ifdef Q_OS_WIN32
  // Request Multimedia Class Schedule Service.
  DwmEnableMMCSS(TRUE);
#endif

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
void PlayerRenderer::setThreadCharacteristics(bool active)
{
#ifdef Q_OS_WIN32
  if (active && !m_hAvrtHandle)
  {
    DWORD handle = 0;
    m_hAvrtHandle = AvSetMmThreadCharacteristicsW(L"Low Latency", &handle);
  }
  else if (!active && m_hAvrtHandle)
  {
    AvRevertMmThreadCharacteristics(m_hAvrtHandle);
    m_hAvrtHandle = 0;
  }
#else
  Q_UNUSED(active);
#endif
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

  if (m_playbackActive != player->m_playbackActive)
  {
    m_playbackActive = player->m_playbackActive;
    setThreadCharacteristics(m_playbackActive);
  }
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
