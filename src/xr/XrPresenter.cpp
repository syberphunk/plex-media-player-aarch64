#include "XrPresenter.h"

#include "system/CapabilityComponent.h"

#include <QProcess>
#include <QSocketNotifier>
#include <QThread>

#include <cmath>

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
  message.windowWide = uint32_t(qMax(0, m_windowWide.load()));
  message.windowTall = uint32_t(qMax(0, m_windowTall.load()));
  if (type == XrProtocol::Mark)
  {
    message.markAcrossMillionths = uint32_t(qMax(0, m_markAcross.load()));
    message.markDownMillionths = uint32_t(qMax(0, m_markDown.load()));
    message.markSay = m_markSay.exchange(false) ? 1 : 0;
  }

  if (::send(m_socket, &message, sizeof(message), MSG_NOSIGNAL) < 0)
    QLOG_WARN() << "Stereoscopic: the presenter did not take the message:" << strerror(errno);
}

QSize XrPresenter::windowPixels() const
{
  return QSize(m_windowWide.load(), m_windowTall.load());
}

void XrPresenter::setWindowPixels(const QSize& pixels)
{
  if (pixels.width() < 2 || pixels.height() < 2)
    return;
  if (pixels.width() == m_windowWide.load() && pixels.height() == m_windowTall.load())
    return;

  m_windowWide = pixels.width();
  m_windowTall = pixels.height();

  QMetaObject::invokeMethod(this, "pushWindow", Qt::QueuedConnection);
}

void XrPresenter::pushWindow()
{
  if (!m_wanted || m_socket < 0)
    return;

  QLOG_INFO() << "Stereoscopic: the window is now" << m_windowWide.load() << "by"
              << m_windowTall.load() << "pixels, telling the presenter";
  tell(XrProtocol::Panel);
}

void XrPresenter::markPointer(double across, double down, bool say)
{
  m_markAcross = int(qBound(0.0, across, 1.0) * 1000000.0);
  m_markDown = int(qBound(0.0, down, 1.0) * 1000000.0);
  if (say)
    m_markSay = true;

  QMetaObject::invokeMethod(this, "pushMark", Qt::QueuedConnection);
}

void XrPresenter::pushMark()
{
  if (!m_wanted || m_socket < 0 || m_panelPointerWide.load() <= 1000)
  {
    m_markSay = false;
    return;
  }

  tell(XrProtocol::Mark);
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

    const bool pointing =
      message.type == XrProtocol::Stereo || message.type == XrProtocol::Pointer;
    const int pointerWide = pointing && message.pointerWideThousandths >= 1000
                              ? int(message.pointerWideThousandths)
                              : 1000;
    const bool pointerNews =
      pointing && (pointerWide != m_panelPointerWide.load() || !m_panelStereo);
    m_panelPointerWide = pointerWide;
    if (pointerNews)
      QLOG_INFO() << "Stereoscopic: the laser's surface is" << float(pointerWide) / 1000.0f
                  << "times as wide as the picture, so the pointer is"
                  << (pointerWide > 1000 ? "spread about the middle of the window"
                                         : "used as delivered");

    const bool spreading = pointing && pointerWide > 1000;
    const bool curved = spreading && message.curveRadiusMillimetres > 0 &&
                        message.curveSweepThousandths > 0 && message.curveTallMillimetres > 0;
    const int aimedBy = curved ? int(message.aimedBy) : int(XrProtocol::Unaimed);
    m_aimRight = curved ? int(message.aimMillimetres[0]) : 0;
    m_aimUp = curved ? int(message.aimMillimetres[1]) : 0;
    m_aimOut = curved ? int(message.aimMillimetres[2]) : 0;
    m_curveRadius = curved ? int(message.curveRadiusMillimetres) : 0;
    m_curveSweep = curved ? int(message.curveSweepThousandths) : 0;
    m_curveTall = curved ? int(message.curveTallMillimetres) : 0;
    m_aimedBy = aimedBy;

    const int curveState = !spreading ? -1 : !curved ? 0 : 1 + aimedBy;
    if (curveState != m_curveSaid)
    {
      m_curveSaid = curveState;
      const QString arc =
        QStringLiteral("an arc of radius %1 m sweeping %2 degrees, %3 m tall")
          .arg(double(message.curveRadiusMillimetres) / 1000.0, 0, 'f', 3)
          .arg(double(message.curveSweepThousandths) * 0.0572958, 0, 'f', 1)
          .arg(double(message.curveTallMillimetres) / 1000.0, 0, 'f', 3);
      if (curveState == 0)
      {
        QLOG_INFO() << "Stereoscopic: the surface the laser lands on is flat, so the pointer "
                       "is spread evenly";
      }
      else if (curveState == 1)
      {
        QLOG_INFO() << "Stereoscopic: the surface the laser lands on is" << qPrintable(arc)
                    << "but nothing says where the laser starts, so the pointer is spread "
                       "as when flat";
      }
      else if (curveState > 1)
      {
        QLOG_INFO() << "Stereoscopic: the surface the laser lands on is" << qPrintable(arc)
                    << "and the picture sits in front of it, so the pointer follows the "
                       "laser from"
                    << qPrintable(pointerSource());
      }
    }

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

      case XrProtocol::Pointer:
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

bool XrPresenter::bendPointer(double across, double down, double& onto, double& under) const
{
  const double spread = double(m_panelPointerWide.load()) / 1000.0;
  const double outer = double(m_curveRadius.load()) / 1000.0;
  const double sweep = double(m_curveSweep.load()) / 1000.0;
  const double tall = double(m_curveTall.load()) / 1000.0;
  if (!(spread > 1.0) || !(outer > 0.0) || !(sweep > 0.0) || !(tall > 0.0) ||
      m_aimedBy.load() == int(XrProtocol::Unaimed))
    return false;

  const double fromRight = double(m_aimRight.load()) / 1000.0;
  const double fromUp = double(m_aimUp.load()) / 1000.0;
  const double fromOut = double(m_aimOut.load()) / 1000.0;
  if (!(fromOut > 0.0))
    return false;

  const double inner = outer / spread;
  const double angle = (across - 0.5) * sweep;
  const double goRight = outer * std::sin(angle) - fromRight;
  const double goUp = (0.5 - down) * tall - fromUp;
  const double goOut = outer * (1.0 - std::cos(angle)) - fromOut;

  const double a = goRight * goRight + goOut * goOut;
  if (!(a > 1.0e-9))
    return false;

  const double b = 2.0 * (fromRight * goRight + (fromOut - inner) * goOut);
  const double c = fromRight * fromRight + (fromOut - inner) * (fromOut - inner) - inner * inner;
  const double inside = b * b - 4.0 * a * c;
  const double along = inside >= 0.0 ? (-b + std::sqrt(inside)) / (2.0 * a) : -b / (2.0 * a);
  if (!(along > 0.0))
    return false;

  const double right = fromRight + along * goRight;
  const double up = fromUp + along * goUp;
  const double out = fromOut + along * goOut;

  onto = std::atan2(right, inner - out) / sweep + 0.5;
  under = 0.5 - up / tall;
  return true;
}

QString XrPresenter::pointerSource() const
{
  const int by = m_aimedBy.load();
  if (m_curveRadius.load() <= 0)
    return QStringLiteral("a flat surface");
  if (by == int(XrProtocol::Unaimed))
    return QStringLiteral("nowhere known");

  return QStringLiteral("%1 m right, %2 m up and %3 m out of its middle, read from %4")
    .arg(double(m_aimRight.load()) / 1000.0, 0, 'f', 3)
    .arg(double(m_aimUp.load()) / 1000.0, 0, 'f', 3)
    .arg(double(m_aimOut.load()) / 1000.0, 0, 'f', 3)
    .arg(by == int(XrProtocol::DashboardDevice)
           ? QStringLiteral("the device the dashboard names")
           : by == int(XrProtocol::PointedController)
               ? QStringLiteral("the controller whose ray lands nearest the pointer")
               : QStringLiteral("the controller pointed nearest the panel"));
}
