#ifndef XRPRESENTER_H
#define XRPRESENTER_H

#include <QObject>
#include <QSize>
#include <QString>

#include <atomic>
#include <stdint.h>

class QProcess;

class XrPresenter : public QObject
{
  Q_OBJECT

public:
  static XrPresenter& Get();
  static bool exists();

  void request(const QSize& videoSize, uint32_t packing, bool swapped, float eyeAspect);
  void release();

  bool wanted() const { return m_wanted; }
  uint32_t generation() const { return m_generation; }
  int takeSocket();

  const QSize& videoSize() const { return m_videoSize; }
  uint32_t packing() const { return m_packing; }
  bool swapped() const { return m_swapped; }
  float eyeAspect() const { return m_eyeAspect; }

  const QString& lastFailure() const { return m_lastFailure; }

  void setOverlayVisible(bool visible) { m_overlayVisible = visible; }
  bool overlayVisible() const { return m_overlayVisible; }

  void wantProbe(const QSize& videoSize);
  void resetProbe();
  bool probeWanted() const { return m_probeWanted; }
  QSize probeSize() const;
  int probeResult() const { return m_probeResult; }

  Q_INVOKABLE void reportFailure(const QString& reason, bool permanent);
  Q_INVOKABLE void reportProbe(int packing);

  Q_SIGNAL void failed();

  Q_SIGNAL void probed();

private Q_SLOTS:
  void shutdown();

private:
  explicit XrPresenter(QObject* parent = nullptr);

  bool spawn();
  void onFinished();

  std::atomic<bool> m_wanted{false};
  std::atomic<uint32_t> m_generation{0};
  std::atomic<bool> m_overlayVisible{true};
  std::atomic<bool> m_impossible{false};

  std::atomic<bool> m_probeWanted{false};
  std::atomic<int> m_probeResult{-1};
  std::atomic<int> m_probeWidth{0};
  std::atomic<int> m_probeHeight{0};

  std::atomic<int> m_socket{-1};
  QProcess* m_process = nullptr;

  QString m_lastFailure;
  QSize m_videoSize;
  uint32_t m_packing = 0;
  bool m_swapped = false;
  float m_eyeAspect = 16.0f / 9.0f;
};

#endif
