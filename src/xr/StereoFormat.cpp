#include "StereoFormat.h"

#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>

namespace
{
  const double kSideBySideFullAspect = 2.6;
  const double kOverUnderFullAspect = 1.1;

  double displayAspect(const QSize& display)
  {
    if (display.width() <= 0 || display.height() <= 0)
      return 0.0;
    return double(display.width()) / double(display.height());
  }

  bool halfFromAspect(StereoFormat::Packing packing, const QSize& display)
  {
    const double aspect = displayAspect(display);
    if (aspect <= 0.0)
      return true;

    if (packing == StereoFormat::Packing::SideBySide)
      return aspect < kSideBySideFullAspect;
    if (packing == StereoFormat::Packing::OverUnder)
      return aspect > kOverUnderFullAspect;
    return true;
  }

  bool fitPlane(const QSizeF& window, const QSizeF& plane, double& scale, double& left,
                double& top)
  {
    if (!(window.width() > 0.0) || !(window.height() > 0.0) || !(plane.width() > 0.0) ||
        !(plane.height() > 0.0))
      return false;

    scale = qMin(plane.width() / window.width(), plane.height() / window.height());
    left = (plane.width() - window.width() * scale) / 2.0;
    top = (plane.height() - window.height() * scale) / 2.0;
    return true;
  }

  QString basename(const QString& path)
  {
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
  }
}

StereoFormat::Layout StereoFormat::fromMpvName(const QString& name, const QSize& display)
{
  Layout layout;
  const QString value = name.trimmed().toLower();

  if (value.isEmpty() || value == QLatin1String("no"))
    return layout;

  if (value == QLatin1String("mono"))
  {
    layout.packing = Packing::Mono;
    return layout;
  }

  if (value == QLatin1String("sbs2l") || value == QLatin1String("sbs2r"))
  {
    layout.packing = Packing::SideBySide;
    layout.swapped = value.endsWith(QLatin1Char('r'));
    layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }

  if (value == QLatin1String("ab2l") || value == QLatin1String("ab2r"))
  {
    layout.packing = Packing::OverUnder;
    layout.swapped = value.endsWith(QLatin1Char('r'));
    layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }

  layout.packing = Packing::Unsupported;
  return layout;
}

StereoFormat::Layout StereoFormat::fromFileName(const QString& path, const QSize& display)
{
  Layout layout;
  const QString name = basename(path);

  static const QRegularExpression halfSideBySide(
    QStringLiteral("(?:\\bh[-_. ]?sbs\\b|\\bhsbs\\b|half[-_. ]?sbs|sbs[-_. ]?half)"),
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression fullSideBySide(
    QStringLiteral("(?:\\bf[-_. ]?sbs\\b|\\bfsbs\\b|full[-_. ]?sbs|sbs[-_. ]?full)"),
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression anySideBySide(
    QStringLiteral("(?:\\bsbs\\b|side[-_. ]?by[-_. ]?side)"),
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression halfOverUnder(
    QStringLiteral("(?:\\bhtab\\b|\\bh[-_. ]?tab\\b|half[-_. ]?ou\\b|half[-_. ]?tab|"
                   "\\bhou\\b|ou[-_. ]?half)"),
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression fullOverUnder(
    QStringLiteral("(?:\\bftab\\b|\\bf[-_. ]?tab\\b|full[-_. ]?ou\\b|full[-_. ]?tab|"
                   "\\bfou\\b|ou[-_. ]?full)"),
    QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression anyOverUnder(
    QStringLiteral("(?:over[-_. ]?under|top[-_. ]?and[-_. ]?bottom|\\btab\\b|\\bou\\b)"),
    QRegularExpression::CaseInsensitiveOption);

  if (halfSideBySide.match(name).hasMatch())
  {
    layout.packing = Packing::SideBySide;
    layout.half = true;
    return layout;
  }
  if (fullSideBySide.match(name).hasMatch())
  {
    layout.packing = Packing::SideBySide;
    layout.half = false;
    return layout;
  }
  if (halfOverUnder.match(name).hasMatch())
  {
    layout.packing = Packing::OverUnder;
    layout.half = true;
    return layout;
  }
  if (fullOverUnder.match(name).hasMatch())
  {
    layout.packing = Packing::OverUnder;
    layout.half = false;
    return layout;
  }
  if (anySideBySide.match(name).hasMatch())
  {
    layout.packing = Packing::SideBySide;
    layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }
  if (anyOverUnder.match(name).hasMatch())
  {
    layout.packing = Packing::OverUnder;
    layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }

  return layout;
}

bool StereoFormat::namedThreeD(const QString& path)
{
  static const QRegularExpression threeD(
    QStringLiteral("(?:\\b3d\\b|\\b3-d\\b)"), QRegularExpression::CaseInsensitiveOption);
  return threeD.match(basename(path)).hasMatch();
}

StereoFormat::Layout StereoFormat::fromAspect(const QSize& display)
{
  Layout layout;
  const double aspect = displayAspect(display);
  if (aspect <= 0.0)
    return layout;

  if (aspect >= kSideBySideFullAspect)
  {
    layout.packing = Packing::SideBySide;
    layout.half = false;
    return layout;
  }
  if (aspect <= kOverUnderFullAspect)
  {
    layout.packing = Packing::OverUnder;
    layout.half = false;
    return layout;
  }

  return layout;
}

StereoFormat::Layout StereoFormat::fromPacking(Packing packing, const QSize& display)
{
  Layout layout;
  if (packing != Packing::SideBySide && packing != Packing::OverUnder)
  {
    layout.packing = packing;
    return layout;
  }

  layout.packing = packing;
  layout.half = halfFromAspect(packing, display);
  return layout;
}

StereoFormat::Layout StereoFormat::resolve(const QString& mpvName, const QString& path,
                                           const QSize& display, QString* source)
{
  const auto answer = [source](const Layout& layout, const char* from) {
    if (source)
      *source = QString::fromLatin1(from);
    return layout;
  };

  const Layout metadata = fromMpvName(mpvName, display);
  if (metadata.isStereo())
    return answer(metadata, "metadata");

  const Layout named = fromFileName(path, display);
  if (named.isStereo())
    return answer(named, "name");

  if (metadata.packing == Packing::Unsupported)
    return answer(metadata, "metadata");

  const Layout aspect = fromAspect(display);
  if (aspect.isStereo())
    return answer(aspect, "aspect");

  if (namedThreeD(path))
    return answer(Layout(), "unclear");

  if (metadata.packing == Packing::Mono)
    return answer(metadata, "metadata");

  return answer(Layout(), "none");
}

StereoFormat::Layout StereoFormat::fromSetting(const QString& setting, const QSize& display)
{
  Layout layout;
  const QString value = setting.trimmed().toLower();

  if (value == QLatin1String("mono"))
  {
    layout.packing = Packing::Mono;
    return layout;
  }
  if (value == QLatin1String("sbs") || value == QLatin1String("sbs_half") ||
      value == QLatin1String("sbs_full"))
  {
    layout.packing = Packing::SideBySide;
    if (value == QLatin1String("sbs_half"))
      layout.half = true;
    else if (value == QLatin1String("sbs_full"))
      layout.half = false;
    else
      layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }
  if (value == QLatin1String("ou") || value == QLatin1String("ou_half") ||
      value == QLatin1String("ou_full"))
  {
    layout.packing = Packing::OverUnder;
    if (value == QLatin1String("ou_half"))
      layout.half = true;
    else if (value == QLatin1String("ou_full"))
      layout.half = false;
    else
      layout.half = halfFromAspect(layout.packing, display);
    return layout;
  }

  return layout;
}

QString StereoFormat::toSetting(const Layout& layout)
{
  switch (layout.packing)
  {
    case Packing::Mono:
      return QStringLiteral("mono");
    case Packing::SideBySide:
      return QStringLiteral("sbs");
    case Packing::OverUnder:
      return QStringLiteral("ou");
    case Packing::Unsupported:
    case Packing::Unknown:
      break;
  }
  return QStringLiteral("auto");
}

QString StereoFormat::label(const Layout& layout)
{
  QString text;
  switch (layout.packing)
  {
    case Packing::Mono:
      return QObject::tr("2D");
    case Packing::SideBySide:
      text = layout.half ? QObject::tr("Side-by-side, half width")
                         : QObject::tr("Side-by-side, full width");
      break;
    case Packing::OverUnder:
      text = layout.half ? QObject::tr("Over-under, half height")
                         : QObject::tr("Over-under, full height");
      break;
    case Packing::Unsupported:
      return QObject::tr("A 3D layout this player cannot split");
    case Packing::Unknown:
      return QObject::tr("Not known");
  }

  if (layout.swapped)
    text += QObject::tr(", right eye first");
  return text;
}

double StereoFormat::eyeAspect(const Layout& layout, const QSize& display)
{
  const double aspect = displayAspect(display);
  if (aspect <= 0.0 || !layout.isStereo())
    return 0.0;
  if (layout.half)
    return aspect;
  return layout.packing == Packing::SideBySide ? aspect / 2.0 : aspect * 2.0;
}

QRectF StereoFormat::eyeBox(const QSizeF& window, const QSizeF& plane)
{
  const QRectF even(0.0, 0.0, window.width() / 2.0, window.height());

  double scale = 0.0;
  double left = 0.0;
  double top = 0.0;
  if (!fitPlane(window, plane, scale, left, top))
    return even;

  const double fromX = -left / scale;
  const double toX = (plane.width() / 2.0 - left) / scale;
  const double fromY = -top / scale;
  const double toY = (plane.height() - top) / scale;
  const double midX = (fromX + toX) / 2.0;
  const double midY = (fromY + toY) / 2.0;
  const double reachX = qMin(midX - qMax(fromX, 0.0), qMin(toX, window.width()) - midX);
  const double reachY = qMin(midY - qMax(fromY, 0.0), qMin(toY, window.height()) - midY);
  if (!(reachX > 0.0) || !(reachY > 0.0))
    return even;

  const double shape = even.width() / even.height();
  const double wide = qMin(2.0 * reachX, 2.0 * reachY * shape);
  const double tall = wide / shape;
  return QRectF(midX - wide / 2.0, midY - tall / 2.0, wide, tall);
}

QPointF StereoFormat::planeAt(const QPointF& at, const QSizeF& window, const QSizeF& plane)
{
  double scale = 0.0;
  double left = 0.0;
  double top = 0.0;
  if (!fitPlane(window, plane, scale, left, top))
  {
    if (!(window.width() > 0.0) || !(window.height() > 0.0))
      return QPointF();
    return QPointF(at.x() / window.width(), at.y() / window.height());
  }

  return QPointF((at.x() * scale + left) / plane.width(),
                 (at.y() * scale + top) / plane.height());
}

QPointF StereoFormat::windowAt(const QPointF& eyeAt, const QSizeF& window, const QSizeF& plane)
{
  double scale = 0.0;
  double left = 0.0;
  double top = 0.0;
  if (!fitPlane(window, plane, scale, left, top))
    return QPointF(eyeAt.x() * window.width(), eyeAt.y() * window.height());

  const QRectF box = eyeBox(window, plane);
  const double fromX = (box.left() * scale + left) / (plane.width() / 2.0);
  const double toX = (box.right() * scale + left) / (plane.width() / 2.0);
  const double fromY = (box.top() * scale + top) / plane.height();
  const double toY = (box.bottom() * scale + top) / plane.height();
  if (!(toX > fromX) || !(toY > fromY))
    return QPointF(eyeAt.x() * window.width(), eyeAt.y() * window.height());

  return QPointF((eyeAt.x() - fromX) / (toX - fromX) * window.width(),
                 (eyeAt.y() - fromY) / (toY - fromY) * window.height());
}
