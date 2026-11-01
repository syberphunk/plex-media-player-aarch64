#ifndef PLAYERQUICKITEM_H
#define PLAYERQUICKITEM_H

#include <Qt>
#include <QRect>
#include <QtQuick/QQuickFramebufferObject>

#include <mpv/client.h>
#include <mpv/render.h>

#include "PlayerComponent.h"
#include "QtHelper.h"
#include "xr/StereoProbe.h"

class PlayerQuickItem;
class QOpenGLFramebufferObject;
class QOpenGLExtraFunctions;
class QOpenGLShaderProgram;

class PlayerRenderer : public QQuickFramebufferObject::Renderer
{
public:
  explicit PlayerRenderer(PlayerQuickItem* item);
  ~PlayerRenderer() override;

  void reportSwap();
  void composeWindow();

protected:
  void render() override;
  void synchronize(QQuickFramebufferObject* item) override;

private:
  bool init();
  void collectGraphicsInfo();

  void syncStereo();
  bool renderPacked(QOpenGLFramebufferObject* target);
  void overlayVideo(QOpenGLExtraFunctions* gl, int half);
  bool prepareKey();
  void drawShot(QOpenGLExtraFunctions* gl, int x, int width, int height);
  void reportShot(QOpenGLExtraFunctions* gl);
  void dumpFrames(QOpenGLExtraFunctions* gl);
  void probePicture(QOpenGLFramebufferObject* fbo);

  PlayerQuickItem* m_item;
  mpv::qt::Handle m_mpv;
  mpv_render_context* m_mpvGL;
  bool m_initialized;
  bool m_playbackActive;
  QString m_graphicsInfo;

  bool m_stereoWanted;
  uint32_t m_stereoPacking;
  bool m_stereoSwapped;
  bool m_stereoControls;
  QSize m_packedSize;
  QSize m_windowSize;
  QRect m_windowVideoRect;
  QOpenGLFramebufferObject* m_windowShot;
  QOpenGLFramebufferObject* m_videoFbo;
  QOpenGLShaderProgram* m_keyProgram;
  unsigned int m_keyVao;
  bool m_keyFailed;
  int m_shotReports;
  int m_dumpsLeft;

  StereoProbe::Detector m_probe;
  QOpenGLFramebufferObject* m_probeTarget;
  QSize m_probeSize;
  bool m_probeWanted;
  bool m_probeRunning;
  int m_probeTick;
};

class PlayerQuickItem : public QQuickFramebufferObject
{
    Q_OBJECT
    friend class PlayerRenderer;

public:
    explicit PlayerQuickItem(QQuickItem* parent = nullptr);
    ~PlayerQuickItem() override;
    Renderer* createRenderer() const override;
    void initMpv(PlayerComponent* player);
    QString debugInfo() { return m_debugInfo; }

signals:
    void onFatalError(QString message);

private slots:
    void onWindowChanged(QQuickWindow* win);
    void onHandleFatalError(QString message);
    void onVideoPlaybackActive(bool active);
    void onFrameSwapped();
    void onAfterRendering();
    void updateVideoGeometry();

private:
    static void on_update(void* ctx);

    mpv::qt::Handle m_mpv;
    QString m_debugInfo;
    bool m_playbackActive;
    PlayerRenderer* m_renderer;
};

#endif
