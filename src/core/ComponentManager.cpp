#include <QObject>
#include <QtQml>
#include <qqmlwebchannel.h>

#include "ComponentManager.h"

#include "power/PowerComponent.h"
#include "server/HTTPServer.h"
#include "input/InputComponent.h"
#include "player/PlayerComponent.h"
#include "display/DisplayComponent.h"
#include "system/SystemComponent.h"
#include "system/UpdateManager.h"
#include "settings/SettingsComponent.h"
#include "remote/RemoteComponent.h"

#include "server/HTTPServer.h"

#include "QsLog.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
ComponentManager::ComponentManager() : QObject(nullptr)
{
  m_qmlProperyMap = QQmlPropertyMap::create(this);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ComponentManager::registerComponent(ComponentBase* comp)
{
  if (m_components.contains(comp->componentName()))
  {
    QLOG_ERROR() << "Component" << comp->componentName() << "already registered!";
    return;
  }
  
  if (comp->componentInitialize())
  {
    QLOG_INFO() << "Component:" << comp->componentName() << "inited";
    m_components[comp->componentName()] = comp;

    // define component as property for qml
    m_qmlProperyMap->insert(comp->componentName(), QVariant::fromValue(comp));
  }
  else
  {
    QLOG_ERROR() << "Failed to init component:" << comp->componentName();
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ComponentManager::initialize()
{
  // then settings, since all other components
  // might have some settings
  //
  registerComponent(&SettingsComponent::Get());

  // start our web server
  auto server = new HttpServer(this);
  server->start();

  registerComponent(&InputComponent::Get());
  registerComponent(&SystemComponent::Get());
  registerComponent(&DisplayComponent::Get());
  registerComponent(&RemoteComponent::Get());
  registerComponent(&PlayerComponent::Get());
  registerComponent(&PowerComponent::Get());
  registerComponent(&UpdateManager::Get());

  for(ComponentBase* component : m_components.values())
    component->componentPostInitialize();
}

/////////////////////////////////////////////////////////////////////////////////////////
void ComponentManager::setWebChannel(QWebChannel* webChannel, const QSet<QString>& reserved)
{
  for(ComponentBase* comp : m_components.values())
  {
    if (!comp->componentExport())
      continue;

    const QString name = QString::fromLatin1(comp->componentName());
    if (reserved.contains(name))
    {
      QLOG_DEBUG() << "Component" << name << "is served by a connector";
      continue;
    }

    QLOG_DEBUG() << "Adding component:" << name << "to webchannel";
    webChannel->registerObject(name, comp);
  }
}
