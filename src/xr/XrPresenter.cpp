#include "XrPresenter.h"

#include "system/CapabilityComponent.h"

#include <QProcess>
#include <QSocketNotifier>
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

  m_notifier = new QSocketNotifier(m_socket, QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this, &XrPresenter::readHost);

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

  QLOG_INFO() << "Stereoscopic: asking the headset to show the Plex panel side by side";
  return true;
}

void XrPresenter::tell(uint32_t type)
{
  if (m_socket < 0)
    return;

  XrProtocol::Message message = XrProtocol::make(XrProtocol::Type(type));
  message.packing = m_packing;
  message.swapped = m_swapped ? 1 : 0;

  if (::send(m_socket, &message, sizeof(message), MSG_NOSIGNAL) < 0)
    QLOG_WARN() << "Stereoscopic: the presenter did not take the message:" << strerror(errno);
}

void XrPresenter::readHost()
{
  while (true)
  {
    XrProtocol::Message message{};
    const ssize_t got = recv(m_socket, &message, sizeof(message), MSG_DONTWAIT);

    if (got == 0)
    {
      QLOG_INFO() << "Stereoscopic: the presenter closed the channel";
      shutdown();
      return;
    }
    if (got < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      if (errno == EINTR)
        continue;
      QLOG_WARN() << "Stereoscopic: the presenter channel failed:" << strerror(errno);
      shutdown();
      return;
    }

    if (got != ssize_t(sizeof(message)) || !XrProtocol::valid(message))
    {
      QLOG_WARN() << "Stereoscopic: a message from the presenter was not understood";
      continue;
    }

    message.reason[XrProtocol::kReasonLength - 1] = '\0';
    const QString reason = QString::fromUtf8(message.reason);

    if (message.mouseWide > 0 && message.mouseTall > 0)
    {
      const QSize pointer(int(message.mouseWide), int(message.mouseTall));
      if (pointer != m_panelPointer)
      {
        m_panelPointer = pointer;
        QLOG_INFO() << "Stereoscopic: the panel's pointer space is" << pointer.width() << "by"
                    << pointer.height() << "pixels";
        QMetaObject::invokeMethod(&CapabilityComponent::Get(), "resurvey",
                                  Qt::QueuedConnection);
      }
    }
    if (message.panelMillimetres > 0)
      m_panelMillimetres = int(message.panelMillimetres);
    if (message.texelAspectThousandths > 0)
      m_panelTexelAspect = float(message.texelAspectThousandths) / 1000.0f;

    switch (message.type)
    {
      case XrProtocol::Stereo:
        m_panelStereo = true;
        m_lastFailure.clear();
        QLOG_INFO() << "Stereoscopic:"
                    << qPrintable(QStringLiteral(
                         "the Plex panel is side by side, flags 0x%1; squeezing the window")
                         .arg(message.flags, 0, 16));
        emit panelChanged();
        break;

      case XrProtocol::Flat:
        if (m_panelStereo)
          QLOG_INFO() << "Stereoscopic: the Plex panel is flat again, the window goes back";
        m_panelStereo = false;
        note(reason, false);
        emit panelChanged();
        break;

      case XrProtocol::Failed:
        m_panelStereo = false;
        note(reason, true);
        emit panelChanged();
        shutdown();
        return;

      default:
        break;
    }
  }
}

void XrPresenter::note(const QString& reason, bool permanent)
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

void XrPresenter::request(const QSize& videoSize, uint32_t packing, bool swapped)
{
  m_packing = packing;
  m_swapped = swapped;
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

  if (videoSize.width() < 2 || videoSize.height() < 2)
    return;

  if (m_wanted && m_process)
  {
    tell(XrProtocol::Panel);
    return;
  }

  if (m_process)
    shutdown();

  m_lastFailure.clear();
  m_wanted = spawn();
  if (m_wanted)
    tell(XrProtocol::Panel);
}

void XrPresenter::release()
{
  if (!m_wanted && !m_process)
    return;

  QLOG_INFO() << "Stereoscopic: the panel no longer needs to be side by side";
  m_panelStereo = false;
  emit panelChanged();
  tell(XrProtocol::Stop);

  if (m_process && QThread::currentThread() == thread() &&
      m_process->state() != QProcess::NotRunning)
  {
    if (m_process->waitForFinished(750))
      QLOG_INFO() << "Stereoscopic: the host left on its own after being asked to stop";
    else
      QLOG_INFO() << "Stereoscopic: the host is still leaving, closing it down";
  }

  shutdown();
}

void XrPresenter::shutdown()
{
  if (QThread::currentThread() != thread())
  {
    m_wanted = false;
    m_panelStereo = false;
    QMetaObject::invokeMethod(this, "shutdown", Qt::QueuedConnection);
    return;
  }

  m_wanted = false;
  m_panelStereo = false;

  if (m_notifier)
  {
    m_notifier->setEnabled(false);
    m_notifier->deleteLater();
    m_notifier = nullptr;
  }

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
