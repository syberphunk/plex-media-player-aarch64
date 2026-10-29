#include "XrPresenter.h"

#include <QProcess>
#include <QThread>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "QsLog.h"
#include "XrHost.h"
#include "XrProtocol.h"

static std::atomic<XrPresenter*> g_presenter{nullptr};

XrPresenter& XrPresenter::Get()
{
  static XrPresenter* instance = new XrPresenter();
  g_presenter.store(instance, std::memory_order_release);
  return *instance;
}

bool XrPresenter::exists()
{
  return g_presenter.load(std::memory_order_acquire) != nullptr;
}

XrPresenter::XrPresenter(QObject* parent) : QObject(parent)
{
  m_rect[0] = 0.0f;
  m_rect[1] = 0.0f;
  m_rect[2] = 1.0f;
  m_rect[3] = 1.0f;
}

void XrPresenter::setVideoRect(float left, float top, float right, float bottom)
{
  if (!(right > left) || !(bottom > top))
    return;

  m_rect[0] = left;
  m_rect[1] = top;
  m_rect[2] = right;
  m_rect[3] = bottom;
  m_haveRect = true;
}

void XrPresenter::videoRect(float out[4]) const
{
  for (int edge = 0; edge < 4; ++edge)
    out[edge] = m_rect[edge];
}

int XrPresenter::takeSocket()
{
  return m_socket.exchange(-1);
}

bool XrPresenter::spawn()
{
  int pair[2] = {-1, -1};
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) < 0)
  {
    QLOG_ERROR() << "Stereoscopic: no channel for the presenter:" << strerror(errno);
    return false;
  }

  QString error;
  QProcess* process = XrHost::startWithFd(QStringLiteral("present"), pair[1], this, &error);
  close(pair[1]);

  if (!process)
  {
    close(pair[0]);
    QLOG_ERROR() << "Stereoscopic: the presenter could not be started:" << qPrintable(error);
    return false;
  }

  m_process = process;
  m_socket = pair[0];

  connect(process, &QProcess::finished, this, &XrPresenter::onFinished);
  connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    QLOG_ERROR() << "Stereoscopic: the presenter failed to run";
    onFinished();
  });
  connect(process, &QProcess::readyReadStandardError, this, [process]() {
    const QByteArray text = process->readAllStandardError();
    for (const QByteArray& line : text.split('\n'))
    {
      if (!line.trimmed().isEmpty())
        QLOG_INFO() << "Stereoscopic:" << qPrintable(QString::fromUtf8(line.trimmed()));
    }
  });

  m_generation++;
  QLOG_INFO() << "Stereoscopic: presenter started, generation" << m_generation.load();
  return true;
}

void XrPresenter::wantProbe(const QSize& videoSize)
{
  if (videoSize.width() < 2 || videoSize.height() < 2)
    return;

  m_probeWidth = videoSize.width();
  m_probeHeight = videoSize.height();
  m_probeWanted = true;
}

void XrPresenter::resetProbe()
{
  m_probeWanted = false;
  m_probeResult = -1;
}

QSize XrPresenter::probeSize() const
{
  return QSize(m_probeWidth, m_probeHeight);
}

void XrPresenter::reportProbe(int packing)
{
  m_probeWanted = false;
  m_probeResult = packing;
  emit probed();
}

void XrPresenter::request(const QSize& videoSize, uint32_t packing, bool swapped, float eyeAspect)
{
  m_overlayVisible = true;
  m_packing = packing;
  m_swapped = swapped;
  m_eyeAspect = eyeAspect > 0.1f && eyeAspect < 10.0f ? eyeAspect : 16.0f / 9.0f;

  const bool sizeChanged = videoSize != m_videoSize;
  m_videoSize = videoSize;

  if (m_impossible)
  {
    m_wanted = false;
    return;
  }

  if (!XrHost::available())
  {
    if (m_wanted)
      QLOG_INFO() << "Stereoscopic: no host route, staying flat";
    m_wanted = false;
    return;
  }

  if (m_wanted && m_process && !sizeChanged)
    return;

  if (m_process)
    shutdown();

  if (videoSize.width() < 2 || videoSize.height() < 2)
    return;

  m_lastFailure.clear();
  m_wanted = spawn();
}

void XrPresenter::release()
{
  if (!m_wanted && !m_process)
    return;

  QLOG_INFO() << "Stereoscopic: presenter no longer wanted";
  shutdown();
}

void XrPresenter::reportFailure(const QString& reason, bool permanent)
{
  if (!reason.isEmpty())
  {
    QLOG_ERROR() << "Stereoscopic:" << qPrintable(reason);
    m_lastFailure = reason;
    emit failed();
  }

  if (permanent)
  {
    m_impossible = true;
    QLOG_INFO() << "Stereoscopic: nothing more will be tried in this session";
  }

  shutdown();
}

void XrPresenter::shutdown()
{
  if (QThread::currentThread() != thread())
  {
    m_wanted = false;
    m_generation++;
    QMetaObject::invokeMethod(this, "shutdown", Qt::QueuedConnection);
    return;
  }

  m_wanted = false;
  m_generation++;

  const int socket = m_socket.exchange(-1);
  if (socket >= 0)
    close(socket);

  if (m_process)
  {
    QProcess* process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning)
    {
      process->terminate();
      if (!process->waitForFinished(1500))
        process->kill();
    }
    process->deleteLater();
  }
}

void XrPresenter::onFinished()
{
  if (!m_process)
    return;
  QLOG_INFO() << "Stereoscopic: the presenter exited";
  shutdown();
}
