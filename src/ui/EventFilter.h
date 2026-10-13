//
// Created by Tobias Hieta on 07/03/16.
//

#ifndef PLEXMEDIAPLAYER_EVENTFILTER_H
#define PLEXMEDIAPLAYER_EVENTFILTER_H

#include <QObject>
#include <QEvent>
#include <QElapsedTimer>

class EventFilter : public QObject
{
  Q_OBJECT
public:
  explicit EventFilter(QObject* parent = nullptr)
    : QObject(parent), m_currentKeyDown(-1), m_lastKeyDown(-1) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  int m_currentKeyDown;
  int m_lastKeyDown;
  QElapsedTimer m_lastKeyDownTime;
};

#endif //PLEXMEDIAPLAYER_EVENTFILTER_H
