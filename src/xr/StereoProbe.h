#ifndef STEREOPROBE_H
#define STEREOPROBE_H

#include <QString>

#include <stdint.h>

namespace StereoProbe
{
  enum class Verdict
  {
    Pending,
    Mono,
    SideBySide,
    OverUnder
  };

  const int kGridWidth = 128;
  const int kGridHeight = 72;
  const int kSamplesWanted = 12;

  class Detector
  {
  public:
    void reset();

    bool add(const uint8_t* rgba, int width, int height);

    bool decided() const { return m_verdict != Verdict::Pending; }
    Verdict verdict() const { return m_verdict; }
    int samples() const { return m_samples; }
    const QString& reading() const { return m_reading; }

  private:
    Verdict m_verdict = Verdict::Pending;
    int m_samples = 0;
    int m_sideBySide = 0;
    int m_overUnder = 0;
    int m_mono = 0;
    QString m_reading;
  };
}

#endif
