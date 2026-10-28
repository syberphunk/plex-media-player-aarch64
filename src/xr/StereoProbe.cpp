#include "StereoProbe.h"

#include <QVector>

#include <cmath>

namespace
{
  const float kFlatFrame = 0.02f;
  const float kMatching = 0.02f;
  const float kRatio = 3.0f;
  const int kVotesWanted = 7;

  float meanAbsoluteDifference(const QVector<float>& a, const QVector<float>& b)
  {
    if (a.isEmpty() || a.size() != b.size())
      return 1.0f;

    float total = 0.0f;
    for (int i = 0; i < a.size(); ++i)
      total += std::fabs(a[i] - b[i]);
    return total / float(a.size());
  }
}

void StereoProbe::Detector::reset()
{
  m_verdict = Verdict::Pending;
  m_samples = 0;
  m_sideBySide = 0;
  m_overUnder = 0;
  m_mono = 0;
  m_reading.clear();
}

bool StereoProbe::Detector::add(const uint8_t* rgba, int width, int height)
{
  if (!rgba || width < 8 || height < 8 || (width & 1) || (height & 1))
    return false;

  const int half = width / 2;
  const int middle = height / 2;

  QVector<float> luma(width * height, 0.0f);
  float total = 0.0f;
  for (int i = 0; i < width * height; ++i)
  {
    const uint8_t* pixel = rgba + i * 4;
    const float value =
      (0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2]) / 255.0f;
    luma[i] = value;
    total += value;
  }

  const float mean = total / float(width * height);
  float spread = 0.0f;
  for (float value : luma)
    spread += std::fabs(value - mean);
  spread /= float(width * height);

  if (spread < kFlatFrame)
    return false;

  float pixelLeftRight = 0.0f;
  for (int y = 0; y < height; ++y)
  {
    const float* row = luma.constData() + y * width;
    for (int x = 0; x < half; ++x)
      pixelLeftRight += std::fabs(row[x] - row[x + half]);
  }
  pixelLeftRight /= float(half * height);

  float pixelTopBottom = 0.0f;
  for (int y = 0; y < middle; ++y)
  {
    const float* top = luma.constData() + y * width;
    const float* bottom = luma.constData() + (y + middle) * width;
    for (int x = 0; x < width; ++x)
      pixelTopBottom += std::fabs(top[x] - bottom[x]);
  }
  pixelTopBottom /= float(width * middle);

  QVector<float> leftRows(height, 0.0f);
  QVector<float> rightRows(height, 0.0f);
  for (int y = 0; y < height; ++y)
  {
    const float* row = luma.constData() + y * width;
    float left = 0.0f;
    float right = 0.0f;
    for (int x = 0; x < half; ++x)
    {
      left += row[x];
      right += row[x + half];
    }
    leftRows[y] = left / float(half);
    rightRows[y] = right / float(half);
  }

  QVector<float> topColumns(width, 0.0f);
  QVector<float> bottomColumns(width, 0.0f);
  for (int x = 0; x < width; ++x)
  {
    float top = 0.0f;
    float bottom = 0.0f;
    for (int y = 0; y < middle; ++y)
    {
      top += luma[y * width + x];
      bottom += luma[(y + middle) * width + x];
    }
    topColumns[x] = top / float(middle);
    bottomColumns[x] = bottom / float(middle);
  }

  const float rowLeftRight = meanAbsoluteDifference(leftRows, rightRows);
  const float columnTopBottom = meanAbsoluteDifference(topColumns, bottomColumns);

  const bool sideBySide = rowLeftRight < kMatching &&
                          rowLeftRight * kRatio < columnTopBottom &&
                          pixelLeftRight < pixelTopBottom;
  const bool overUnder = columnTopBottom < kMatching &&
                         columnTopBottom * kRatio < rowLeftRight &&
                         pixelTopBottom < pixelLeftRight;

  if (sideBySide && !overUnder)
    ++m_sideBySide;
  else if (overUnder && !sideBySide)
    ++m_overUnder;
  else
    ++m_mono;

  ++m_samples;

  m_reading = QStringLiteral("rows %1 columns %2 pixels %3/%4 spread %5")
                .arg(rowLeftRight, 0, 'f', 4)
                .arg(columnTopBottom, 0, 'f', 4)
                .arg(pixelLeftRight, 0, 'f', 4)
                .arg(pixelTopBottom, 0, 'f', 4)
                .arg(spread, 0, 'f', 4);

  if (m_samples < kSamplesWanted)
    return true;

  if (m_sideBySide >= kVotesWanted)
    m_verdict = Verdict::SideBySide;
  else if (m_overUnder >= kVotesWanted)
    m_verdict = Verdict::OverUnder;
  else
    m_verdict = Verdict::Mono;

  m_reading = QStringLiteral("%1, %2 of %3 side by side, %4 over-under")
                .arg(m_reading)
                .arg(m_sideBySide)
                .arg(m_samples)
                .arg(m_overUnder);
  return true;
}
