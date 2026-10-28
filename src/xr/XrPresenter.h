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
  void recentre();

  bool wanted() const { return m_wanted; }
  uint32_t generation() const { return m_generation; }
  int takeSocket();

  const QSize& videoSize() const { return m_videoSize; }
  uint32_t packing() const { return m_packing; }
  bool swapped() const { return m_swapped; }
  float eyeAspect() const { return m_eyeAspect; }
  uint32_t recentreCount() const { return m_recentreCount; }

  Q_INVOKABLE void reportFailure(const QString& reason);

private Q_SLOTS:
  void shutdown();

private:
  explicit XrPresenter(QObject* parent = nullptr);

  bool spawn();
  void onFinished();

  std::atomic<bool> m_wanted{false};
  std::atomic<uint32_t> m_generation{0};
  std::atomic<uint32_t> m_recentreCount{0};

  std::atomic<int> m_socket{-1};
  QProcess* m_process = nullptr;

  QSize m_videoSize;
  uint32_t m_packing = 0;
  bool m_swapped = false;
  float m_eyeAspect = 16.0f / 9.0f;
};

#endif
