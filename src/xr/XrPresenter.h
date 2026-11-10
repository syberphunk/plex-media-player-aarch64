#ifndef XRPRESENTER_H
#define XRPRESENTER_H

#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <stdint.h>

class QProcess;
class QSocketNotifier;

class XrPresenter : public QObject
{
  Q_OBJECT

public:
  static XrPresenter& Get();
  static bool exists();

  void request(const QSize& videoSize, uint32_t packing, bool swapped);
  void release();

  bool wanted() const { return m_wanted; }
  bool panelStereo() const { return m_panelStereo; }

  QSize panelPointer() const { return m_panelPointer; }
  int panelMillimetres() const { return m_panelMillimetres; }
  float panelTexelAspect() const { return m_panelTexelAspect; }
  float panelPointerWide() const { return float(m_panelPointerWide.load()) / 1000.0f; }
  bool bendPointer(double across, double down, double& onto, double& under) const;
  QString pointerSource() const;
  void markPointer(double across, double down, bool say);

  const QSize& videoSize() const { return m_videoSize; }
  uint32_t packing() const { return m_packing; }
  bool swapped() const { return m_swapped; }

  const QString& lastFailure() const { return m_lastFailure; }

  void setWindowPixels(const QSize& pixels);
  QSize windowPixels() const;

  void setControlsActive(bool active) { m_controlsActive = active; }
  bool controlsActive() const { return m_controlsActive; }

  void wantProbe(const QSize& videoSize);
  void resetProbe();
  bool probeWanted() const { return m_probeWanted; }
  QSize probeSize() const;
  int probeResult() const { return m_probeResult; }

  Q_INVOKABLE void reportProbe(int packing);

  Q_SIGNAL void failed();

  Q_SIGNAL void probed();

  Q_SIGNAL void panelChanged();

private Q_SLOTS:
  void shutdown();
  void readHost();
  void pushWindow();
  void pushMark();

private:
  explicit XrPresenter(QObject* parent = nullptr);

  bool spawn();
  void onFinished();
  void tell(uint32_t type);
  void note(const QString& reason, bool permanent);

  std::atomic<bool> m_wanted{false};
  std::atomic<bool> m_panelStereo{false};
  std::atomic<bool> m_controlsActive{false};
  std::atomic<bool> m_impossible{false};

  std::atomic<bool> m_probeWanted{false};
  std::atomic<int> m_probeResult{-1};
  std::atomic<int> m_probeWidth{0};
  std::atomic<int> m_probeHeight{0};

  std::atomic<int> m_windowWide{0};
  std::atomic<int> m_windowTall{0};
  std::atomic<int> m_panelPointerWide{1000};
  std::atomic<int> m_curveRadius{0};
  std::atomic<int> m_curveSweep{0};
  std::atomic<int> m_curveTall{0};
  std::atomic<int> m_aimedBy{0};
  std::atomic<int> m_aimRight{0};
  std::atomic<int> m_aimUp{0};
  std::atomic<int> m_aimOut{0};
  int m_curveSaid = -1;
  std::atomic<int> m_markAcross{0};
  std::atomic<int> m_markDown{0};
  std::atomic<bool> m_markSay{false};

  int m_socket = -1;
  QSocketNotifier* m_notifier = nullptr;
  QProcess* m_process = nullptr;

  QString m_lastFailure;
  QSize m_videoSize;
  QSize m_panelPointer;
  int m_panelMillimetres = 0;
  float m_panelTexelAspect = 0.0f;
  uint32_t m_packing = 0;
  bool m_swapped = false;
};

#endif
