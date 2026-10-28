#include "XrPresenter.h"

#include <QProcess>

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "QsLog.h"
#include "XrHost.h"
#include "XrProtocol.h"

XrPresenter& XrPresenter::Get()
{
  static XrPresenter* instance = new XrPresenter();
  return *instance;
}

XrPresenter::XrPresenter(QObject* parent) : QObject(parent)
{
}

int XrPresenter::takeSocket()
{
  const int fd = m_socket;
  m_socket = -1;
  return fd;
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
  QLOG_INFO() << "Stereoscopic: presenter started, generation" << m_generation;
  return true;
}

void XrPresenter::request(const QSize& videoSize, uint32_t packing, bool swapped, float eyeAspect)
{
  m_packing = packing;
  m_swapped = swapped;
  m_eyeAspect = eyeAspect > 0.1f && eyeAspect < 10.0f ? eyeAspect : 16.0f / 9.0f;

  const bool sizeChanged = videoSize != m_videoSize;
  m_videoSize = videoSize;

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

  m_wanted = spawn();
}

void XrPresenter::release()
{
  if (!m_wanted && !m_process)
    return;

  QLOG_INFO() << "Stereoscopic: presenter no longer wanted";
  shutdown();
}

void XrPresenter::recentre()
{
  m_recentreCount++;
}

void XrPresenter::reportFailure(const QString& reason)
{
  if (!reason.isEmpty())
    QLOG_ERROR() << "Stereoscopic:" << qPrintable(reason);
  shutdown();
}

void XrPresenter::shutdown()
{
  m_wanted = false;
  m_generation++;

  if (m_socket >= 0)
  {
    close(m_socket);
    m_socket = -1;
  }

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
