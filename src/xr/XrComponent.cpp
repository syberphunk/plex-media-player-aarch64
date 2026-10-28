#include "XrComponent.h"

#include <QProcess>
#include <QTimer>

#include "QsLog.h"
#include "XrHost.h"
#include "XrSupport.h"
#include "settings/SettingsComponent.h"

namespace
{
  const char* const kSection = "video";
  const char* const kKey = "stereoscopic";
  const char* const kFormatKey = "stereoscopic_format";
  const char* const kSwapKey = "stereoscopic_swap";

  const int kProbeTimeoutMs = 8000;
  const int kSessionTimeoutMs = 20000;
}

XrComponent::XrComponent(QObject* parent) : ComponentBase(parent)
{
}

bool XrComponent::componentInitialize()
{
  return true;
}

void XrComponent::ensureProbed()
{
  if (!supported() || !enabled())
    return;

  if (m_checked || m_process)
    return;

  probe(false);
}

bool XrComponent::supported() const
{
  return XrSupport::autoProbeAllowed();
}

bool XrComponent::enabled() const
{
  return SettingsComponent::Get().value(kSection, kKey).toBool();
}

void XrComponent::setEnabled(bool value)
{
  if (enabled() == value)
    return;

  SettingsComponent::Get().setValue(kSection, kKey, value);
  if (value)
    probe(false);
  emit stateChanged();
}

QString XrComponent::formatOverride() const
{
  const QString value = SettingsComponent::Get().value(kSection, kFormatKey).toString();
  return value.isEmpty() ? QStringLiteral("auto") : value;
}

void XrComponent::setFormatOverride(const QString& value)
{
  if (formatOverride() == value)
    return;

  SettingsComponent::Get().setValue(kSection, kFormatKey, value);
  emit formatChanged();
  emit overrideChanged();
}

bool XrComponent::swapEyes() const
{
  return SettingsComponent::Get().value(kSection, kSwapKey).toBool();
}

void XrComponent::setSwapEyes(bool value)
{
  if (swapEyes() == value)
    return;

  SettingsComponent::Get().setValue(kSection, kSwapKey, value);
  emit formatChanged();
  emit overrideChanged();
}

void XrComponent::setDetected(const QString& label, const QString& source, bool stereo)
{
  if (m_detectedLabel == label && m_detectedSource == source && m_detectedStereo == stereo)
    return;

  m_detectedLabel = label;
  m_detectedSource = source;
  m_detectedStereo = stereo;
  emit formatChanged();
}

void XrComponent::clearDetected()
{
  setDetected(QString(), QString(), false);
}

bool XrComponent::runtimeFound() const
{
  return XrSupport::probe().runtimeFound;
}

bool XrComponent::headsetPresent() const
{
  return XrSupport::probe().headsetPresent;
}

bool XrComponent::sessionReady() const
{
  return XrSupport::probe().sessionReady;
}

bool XrComponent::viaHost() const
{
  return XrSupport::probe().viaHost;
}

QString XrComponent::runtimeName() const
{
  const XrSupport::Info& info = XrSupport::probe();
  if (!info.runtimeFound)
    return QString();
  return info.runtimeVersion.isEmpty()
           ? info.runtimeName
           : QStringLiteral("%1 %2").arg(info.runtimeName, info.runtimeVersion);
}

QString XrComponent::headsetName() const
{
  return XrSupport::probe().systemName;
}

QString XrComponent::perEye() const
{
  const QSize size = XrSupport::probe().recommendedPerEye;
  if (size.isEmpty())
    return QString();
  return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

QString XrComponent::lastError() const
{
  return XrSupport::probe().error;
}

void XrComponent::probe(bool withSession)
{
  if (m_process)
    return;

  QLOG_INFO() << "OpenXR probe starting:"
              << qPrintable(QStringLiteral("route=%1 session=%2")
                              .arg(XrSupport::hostRoute() ? "host" : "local",
                                   withSession ? "yes" : "no"));

  if (!XrSupport::hostRoute())
  {
    XrSupport::refreshLocal();
    m_checked = true;
    if (withSession)
      m_sessionTested = true;
    QLOG_INFO() << "OpenXR probe finished:" << qPrintable(XrSupport::report());
    emit stateChanged();
    return;
  }

  QString error;
  m_withSession = withSession;
  m_timedOut = false;
  m_process = XrHost::start(withSession ? QStringLiteral("session") : QStringLiteral("probe"),
                            this, &error);
  if (!m_process)
  {
    QLOG_WARN() << "OpenXR probe could not start:" << qPrintable(error);
    XrSupport::applyHostAnswer(QJsonObject(), error, withSession);
    m_checked = true;
    emit stateChanged();
    return;
  }

  const int limit = withSession ? kSessionTimeoutMs : kProbeTimeoutMs;
  m_timeout = new QTimer(this);
  m_timeout->setSingleShot(true);
  connect(m_timeout, &QTimer::timeout, this, [this]() {
    if (!m_process)
      return;
    m_timedOut = true;
    m_process->kill();
  });
  m_timeout->start(limit);
  m_timeoutMs = limit;

  connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    if (m_process)
      finish(m_process->errorString());
  });
  connect(m_process, &QProcess::finished, this, [this]() { finish(QString()); });

  emit stateChanged();
}

void XrComponent::finish(const QString& startError)
{
  if (!m_process)
    return;

  QProcess* process = m_process;
  m_process = nullptr;
  process->disconnect(this);

  if (m_timeout)
  {
    m_timeout->stop();
    m_timeout->deleteLater();
    m_timeout = nullptr;
  }

  QString error = startError;
  QJsonObject answer;
  if (m_timedOut)
  {
    error = tr("the headset did not answer in %1 seconds").arg(m_timeoutMs / 1000);
  }
  else if (error.isEmpty())
  {
    answer = XrHost::parseAnswer(process->readAllStandardOutput(),
                                 process->readAllStandardError(),
                                 process->exitCode(), &error);
  }

  process->deleteLater();

  XrSupport::applyHostAnswer(answer, error, m_withSession);
  m_checked = true;
  if (m_withSession)
    m_sessionTested = true;

  QLOG_INFO() << "OpenXR probe finished:"
              << qPrintable(error.isEmpty() ? QStringLiteral("ok")
                                            : QStringLiteral("error=%1").arg(error));

  emit stateChanged();
}

void XrComponent::refresh()
{
  if (!supported())
    return;
  probe(m_sessionTested);
}

void XrComponent::testOutput()
{
  if (!supported())
    return;
  probe(true);
}

QString XrComponent::report() const
{
  QStringList parts;
  parts << QStringLiteral("enabled=%1").arg(enabled() ? "yes" : "no");
  if (XrHost::inSandbox())
    parts << QStringLiteral("helper=%1").arg(XrHost::helperPath());
  parts << XrSupport::report();
  return parts.join(QLatin1Char(' '));
}
