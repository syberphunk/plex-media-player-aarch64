#ifndef __CONNECTOR_MANAGER_H__
#define __CONNECTOR_MANAGER_H__

#include <QList>
#include <QObject>
#include <QWebChannel>
#include <QWindow>

#include "utils/Utils.h"

class ConnectorBase;
class SystemConnector;

class ConnectorManager : public QObject
{
  Q_OBJECT
  DEFINE_SINGLETON(ConnectorManager);

public:
  void initialize();
  void setWebChannel(QWebChannel* webChannel);

  void trackWindow(QWindow* window);

private:
  ConnectorManager();

  QList<ConnectorBase*> m_connectors;
  SystemConnector* m_system = nullptr;
};

#endif
