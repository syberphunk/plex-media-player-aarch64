#ifndef XRCOMPONENT_H
#define XRCOMPONENT_H

#include <QSize>
#include <QString>

#include "ComponentManager.h"
#include "utils/Utils.h"

class QProcess;
class QTimer;

class XrComponent : public ComponentBase
{
  Q_OBJECT
  DEFINE_SINGLETON(XrComponent);

public:
  const char* componentName() override { return "xr"; }
  bool componentExport() override { return false; }
  bool componentInitialize() override;
  void componentPostInitialize() override;

  Q_PROPERTY(bool supported READ supported CONSTANT)
  Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY stateChanged)
  Q_PROPERTY(bool runtimeFound READ runtimeFound NOTIFY stateChanged)
  Q_PROPERTY(bool headsetPresent READ headsetPresent NOTIFY stateChanged)
  Q_PROPERTY(bool sessionReady READ sessionReady NOTIFY stateChanged)
  Q_PROPERTY(bool checked READ checked NOTIFY stateChanged)
  Q_PROPERTY(bool sessionTested READ sessionTested NOTIFY stateChanged)
  Q_PROPERTY(bool viaHost READ viaHost NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(QString runtimeName READ runtimeName NOTIFY stateChanged)
  Q_PROPERTY(QString headsetName READ headsetName NOTIFY stateChanged)
  Q_PROPERTY(QString perEye READ perEye NOTIFY stateChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

  bool supported() const;
  bool enabled() const;
  void setEnabled(bool enabled);
  bool runtimeFound() const;
  bool headsetPresent() const;
  bool sessionReady() const;
  bool checked() const { return m_checked; }
  bool sessionTested() const { return m_sessionTested; }
  bool viaHost() const;
  bool busy() const { return m_process != nullptr; }
  QString runtimeName() const;
  QString headsetName() const;
  QString perEye() const;
  QString lastError() const;

  Q_INVOKABLE void refresh();

  Q_INVOKABLE void testOutput();

  Q_INVOKABLE QString report() const;

  Q_SIGNAL void stateChanged();

private:
  explicit XrComponent(QObject* parent = nullptr);

  void probe(bool withSession);
  void finish(const QString& error);

  QProcess* m_process = nullptr;
  QTimer* m_timeout = nullptr;
  bool m_withSession = false;
  bool m_checked = false;
  bool m_sessionTested = false;
  bool m_timedOut = false;
  int m_timeoutMs = 0;
};

#endif
