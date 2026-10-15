#include "ConnectorManager.h"

#include "ConnectorBase.h"
#include "Connectors.h"
#include "QsLog.h"

ConnectorManager::ConnectorManager() : QObject(nullptr)
{
}

void ConnectorManager::initialize()
{
  if (!m_connectors.isEmpty())
    return;

  m_system = new SystemConnector(this);

  m_connectors << m_system
               << new SettingsConnector(this)
               << new PlayerConnector(this)
               << new StorageConnector(this)
               << new InputConnector(this)
               << new DisplayConnector(this)
               << new ServerConnector(this)
               << new UpdateConnector(this)
               << new UserConnector(this);
}

void ConnectorManager::setWebChannel(QWebChannel* webChannel)
{
  if (!webChannel)
    return;

  for (ConnectorBase* connector : m_connectors)
  {
    QLOG_DEBUG() << "Adding connector:" << connector->connectorName() << "to webchannel";
    webChannel->registerObject(connector->connectorName(), connector);
  }
}

QSet<QString> ConnectorManager::connectorNames() const
{
  QSet<QString> names;
  for (ConnectorBase* connector : m_connectors)
    names.insert(connector->connectorName());
  return names;
}

void ConnectorManager::trackWindow(QWindow* window)
{
  if (m_system)
    m_system->trackWindow(window);
}
