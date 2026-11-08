#ifndef PLAYERQUICKITEM_H
#define PLAYERQUICKITEM_H

#include <Qt>
#include <QList>
#include <QPointF>
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
class QTimer;

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
  bool prepareArrow();
  void drawRuler(QOpenGLExtraFunctions* gl, int half);
  void drawCursor(QOpenGLExtraFunctions* gl, int half);
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
  int m_dumpBudget;
  bool m_captures;
  QPointF m_cursorAt;
  qint64 m_rulerFrom;
  int m_rulerSaid;
  QOpenGLShaderProgram* m_arrowProgram;
  unsigned int m_arrowVao;
  bool m_arrowFailed;

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
    void notePointerReach();
    void notePointerRest();
    void reportPointerReach(const char* when);
    void reportPointerPress();
    QPointF stretchPointer(const QPointF& at) const;
    bool deliveredSpan(float wide, float& low, float& high) const;
    void noteRawReach(const QPointF& at);

    bool eventFilter(QObject* watched, QEvent* event) override;
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
    void onPanelChanged();
    void onCursorIdle();
    void onCursorRest();
    void onSettingsChanged(const QString& section, const QVariantMap& values);
    void updateVideoGeometry();

private:
    static void on_update(void* ctx);

    mpv::qt::Handle m_mpv;
    QString m_debugInfo;
    bool m_playbackActive;
    bool m_stereoLive;
    QPointF m_cursorAt;
    QTimer* m_cursorIdle;
    QTimer* m_cursorRest;
    QPointF m_cursorRestedAt;
    QPointF m_cursorRaw;
    bool m_cursorStretching;
    float m_cursorRawLow;
    float m_cursorRawHigh;
    QPointF m_cursorRawLast;
    bool m_cursorRawSeen;
    bool m_cursorOffWindow;
    bool m_cursorPinLeft;
    bool m_cursorPinRight;
    float m_cursorPinFloor;
    float m_cursorPinCeiling;
    bool m_cursorSpanSaid;
    int m_cursorPinned;
    int m_cursorPinSaid;
    QList<QPointF> m_cursorTrail;
    int m_cursorRests;
    bool m_cursorHideLogged;
    float m_cursorSeenLow;
    float m_cursorSeenHigh;
    int m_cursorReports;
    qint64 m_cursorReportedAt;
    bool m_cursorHitLeft;
    bool m_cursorHitRight;
    float m_cursorSeenTop;
    float m_cursorSeenBase;
    bool m_cursorHitTop;
    bool m_cursorHitBase;
    bool m_captures;
    PlayerRenderer* m_renderer;
};

#endif
