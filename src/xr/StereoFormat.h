#ifndef STEREOFORMAT_H
#define STEREOFORMAT_H

#include <QSize>
#include <QString>

namespace StereoFormat
{
  enum class Packing
  {
    Unknown,
    Mono,
    SideBySide,
    OverUnder,
    Unsupported
  };

  struct Layout
  {
    Packing packing = Packing::Unknown;
    bool half = true;
    bool swapped = false;

    bool isStereo() const
    {
      return packing == Packing::SideBySide || packing == Packing::OverUnder;
    }

    bool operator==(const Layout& other) const
    {
      return packing == other.packing && half == other.half && swapped == other.swapped;
    }

    bool operator!=(const Layout& other) const { return !(*this == other); }
  };

  Layout fromMpvName(const QString& name, const QSize& display);

  Layout fromFileName(const QString& path, const QSize& display);

  Layout fromAspect(const QSize& display);

  bool namedThreeD(const QString& path);

  Layout resolve(const QString& mpvName, const QString& path, const QSize& display,
                 QString* source);

  Layout fromSetting(const QString& setting);

  QString toSetting(const Layout& layout);

  QString label(const Layout& layout);
}

#endif
