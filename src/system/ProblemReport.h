#ifndef PROBLEMREPORT_H
#define PROBLEMREPORT_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

class ReportRedactor
{
public:
  void learn(const QString& text);
  void learnValue(const QString& kind, const QString& value);
  QString redact(const QString& text);
  QStringList summary() const;

private:
  struct Kind
  {
    QString prefix;
    QHash<QString, QString> standIns;
    int places = 0;
  };

  QString standIn(const QString& kind, const QString& value);
  QString replaceLearned(const QString& text, const QString& kind, bool wholeWord);
  QString replaceIp(const QString& ip);

  QMap<QString, Kind> m_kinds;
  QMap<QString, QStringList> m_learned;
};

struct ReportFile
{
  QString name;
  QString source;
  QByteArray data;
  bool redact = true;
};

namespace ProblemReport
{
  QList<ReportFile> collect(bool pictures, const QString& summary);
  QString write(const QList<ReportFile>& files, const QString& directory, QString* error);
  QString defaultDirectory();
};

#endif
