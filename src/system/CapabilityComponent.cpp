#include "CapabilityComponent.h"

#include <QStringList>
#include <QWindow>

#include "QsLog.h"
#include "display/DisplayComponent.h"
#include "settings/SettingsComponent.h"
#include "shared/SessionEnv.h"
#include "xr/XrPresenter.h"

using Capability::Finding;
using Capability::Weight;

namespace
{
  int compareVersions(const QString& left, const QString& right)
  {
    const QStringList ours = left.split('.');
    const QStringList theirs = right.split('.');

    for (int part = 0; part < qMax(ours.size(), theirs.size()); ++part)
    {
      bool haveOurs = false;
      bool haveTheirs = false;
      const int a = part < ours.size() ? ours.at(part).toInt(&haveOurs) : 0;
      const int b = part < theirs.size() ? theirs.at(part).toInt(&haveTheirs) : 0;

      if (part < ours.size() && !haveOurs)
        return 0;
      if (part < theirs.size() && !haveTheirs)
        return 0;
      if (a != b)
        return a < b ? -1 : 1;
    }
    return 0;
  }

  void add(QList<Finding>& into, const char* id, Weight weight, const QString& text,
           const QString& remedy = QString())
  {
    Finding finding;
    finding.id = QString::fromLatin1(id);
    finding.weight = weight;
    finding.text = text;
    finding.remedy = remedy;
    into.append(finding);
  }
}

CapabilityComponent::CapabilityComponent(QObject* parent) : ComponentBase(parent)
{
}

bool CapabilityComponent::componentInitialize()
{
  return true;
}

void CapabilityComponent::setDismissed(bool value)
{
  if (m_dismissed == value)
    return;
  m_dismissed = value;
  emit surveyed();
}

QStringList CapabilityComponent::warnings() const
{
  QStringList out;
  for (const Finding& finding : m_findings)
  {
    if (finding.weight == Weight::Warning)
      out << finding.text;
  }
  return out;
}

QStringList CapabilityComponent::remedies() const
{
  QStringList out;
  for (const Finding& finding : m_findings)
  {
    if (finding.weight == Weight::Warning && !finding.remedy.isEmpty() &&
        !out.contains(finding.remedy))
      out << finding.remedy;
  }
  return out;
}

bool CapabilityComponent::belowMinimum() const
{
  for (const Finding& finding : m_findings)
  {
    if (finding.weight == Weight::Warning)
      return true;
  }
  return false;
}

void CapabilityComponent::survey(const QSize& window, const QString& codec,
                                 const QString& pixelFormat, int videoBitDepth,
                                 bool stereoWanted)
{
  m_lastWindow = window;
  m_lastCodec = codec;
  m_lastFormat = pixelFormat;
  m_lastDepth = videoBitDepth;
  m_lastStereo = stereoWanted;
  m_surveyed = true;

  QList<Finding> found;

  const QSize panelPointer =
    XrPresenter::exists() ? XrPresenter::Get().panelPointer() : QSize();

  if (window.isValid())
  {
    if (window.width() < Capability::Minimum::kWindowWide ||
        window.height() < Capability::Minimum::kWindowTall)
    {
      add(found, "window-small", stereoWanted ? Weight::Warning : Weight::Note,
          QStringLiteral("The display is %1 by %2, below the %3 by %4 this build is tested at.")
            .arg(window.width())
            .arg(window.height())
            .arg(Capability::Minimum::kWindowWide)
            .arg(Capability::Minimum::kWindowTall),
          QStringLiteral("Set the display resolution to %1 by %2.")
            .arg(Capability::Minimum::kWindowWide)
            .arg(Capability::Minimum::kWindowTall));
    }

    if (stereoWanted)
    {
      const int eyeWide = window.width() / 2;
      if (eyeWide < Capability::Minimum::kStereoEyeWide ||
          window.height() < Capability::Minimum::kStereoEyeTall)
      {
        add(found, "eye-small", Weight::Note,
            QStringLiteral("Side by side gives each eye %1 by %2 pixels, below %3 by %4.")
              .arg(eyeWide)
              .arg(window.height())
              .arg(Capability::Minimum::kStereoEyeWide)
              .arg(Capability::Minimum::kStereoEyeTall));
      }
    }
  }

  QStringList codecs = SessionEnv::v4l2DecodeCodecs();
  if (SettingsComponent::Get().value(SETTINGS_SECTION_MAIN, "vp9HardwareDecoding").toBool() &&
      SessionEnv::v4l2DecodeBrokenCodecs().contains("vp9"))
    codecs << "vp9";
  const int depth = SessionEnv::v4l2DecodeBitDepth();

  if (codecs.isEmpty())
  {
    add(found, "no-hwdec", Weight::Warning,
        QStringLiteral("No hardware video decoder was found, so playback runs on the CPU."));
  }
  else if (!codec.isEmpty())
  {
    const QString family = codec.section('.', 0, 0).toLower();
    bool listed = false;
    for (const QString& have : codecs)
    {
      if (family.contains(have, Qt::CaseInsensitive) ||
          have.contains(family, Qt::CaseInsensitive))
        listed = true;
    }

    if (!listed)
    {
      add(found, "codec-unlisted", Weight::Warning,
          QStringLiteral("This file is %1, which the hardware decoder does not list (%2).")
            .arg(codec, codecs.join(QStringLiteral(", "))));
    }
    else if (family.contains(QLatin1String("vp9")) &&
             SessionEnv::drmDriver().startsWith(QLatin1String("msm")))
    {
      add(found, "vp9-slow", Weight::Warning,
          QStringLiteral("VP9 hardware decoding is advertised on this device but does not keep "
                         "up, so this file may stutter."));
    }
  }

  if (depth > 0 && videoBitDepth > depth)
  {
    add(found, "depth", Weight::Warning,
        QStringLiteral("This file is %1 bit%2 and the hardware decoder emits %3 bit at most.")
          .arg(videoBitDepth)
          .arg(pixelFormat.isEmpty() ? QString()
                                     : QStringLiteral(" (%1)").arg(pixelFormat))
          .arg(depth));
  }

  const QString osVersion = SessionEnv::hostOsVersion();
  if (osVersion.isEmpty())
  {
    add(found, "os-unknown", Weight::Note,
        QStringLiteral("The device's operating system version could not be read."));
  }
  else if (compareVersions(osVersion,
                           QString::fromLatin1(Capability::Minimum::kSteamOsVersion)) < 0)
  {
    add(found, "os-old", Weight::Warning,
        QStringLiteral("The device runs version %1, below the %2 this build is tested against.")
          .arg(osVersion, QString::fromLatin1(Capability::Minimum::kSteamOsVersion)),
        QStringLiteral("Update the device, then restart Plex."));
  }

  QStringList parts;
  parts << QStringLiteral("window=%1x%2")
             .arg(window.width())
             .arg(window.height());
  if (panelPointer.isValid())
  {
    parts << QStringLiteral("panel-pointer=%1x%2")
               .arg(panelPointer.width())
               .arg(panelPointer.height());
  }
  parts << QStringLiteral("host-os=%1").arg(SessionEnv::hostOsReport());
  parts << QStringLiteral("drm=%1").arg(SessionEnv::drmDriver());
  parts << QStringLiteral("hwdec=%1")
             .arg(codecs.isEmpty() ? QStringLiteral("none") : codecs.join('/'));
  parts << QStringLiteral("hwdec-depth=%1").arg(depth);
  if (!codec.isEmpty())
    parts << QStringLiteral("codec=%1").arg(codec);
  if (videoBitDepth > 0)
    parts << QStringLiteral("file-depth=%1").arg(videoBitDepth);
  parts << QStringLiteral("stereo=%1").arg(stereoWanted ? "yes" : "no");
  m_environment = parts.join(' ');

  QStringList key;
  for (const Finding& finding : found)
    key << finding.id;
  const QString nowKey = key.join(',') + '|' + m_environment;

  m_findings = found;

  if (nowKey != m_lastKey)
  {
    m_lastKey = nowKey;
    m_dismissed = false;

    QLOG_INFO() << "Capability:" << qPrintable(m_environment);
    for (const Finding& finding : m_findings)
    {
      if (finding.weight == Weight::Warning)
        QLOG_WARN() << "Capability:" << qPrintable(finding.id) << "-"
                    << qPrintable(finding.text);
      else
        QLOG_INFO() << "Capability:" << qPrintable(finding.id) << "-"
                    << qPrintable(finding.text);
    }

    emit surveyed();
  }
}

void CapabilityComponent::resurvey()
{
  if (!m_surveyed)
    return;

  QSize window = m_lastWindow;
  const QWindow* live = DisplayComponent::Get().applicationWindow();
  if (live && live->width() > 0 && live->height() > 0)
    window = QSize(live->width(), live->height());

  survey(window, m_lastCodec, m_lastFormat, m_lastDepth, m_lastStereo);
}

QString CapabilityComponent::report() const
{
  QStringList lines;
  lines << m_environment;
  for (const Finding& finding : m_findings)
  {
    lines << QStringLiteral("%1 %2 %3")
               .arg(finding.weight == Weight::Warning ? QStringLiteral("warning")
                                                      : QStringLiteral("note"),
                    finding.id, finding.text);
  }
  return lines.join('\n');
}
