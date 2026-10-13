//
// Created by Tobias Hieta on 02/02/16.
//

#ifndef PLEXMEDIAPLAYER_ROKUREMOTECOMPONENT_H
#define PLEXMEDIAPLAYER_ROKUREMOTECOMPONENT_H

#include "input/InputComponent.h"
#include "server/HttpListener.h"
#include <QUdpSocket>

class InputRoku : public InputBase
{
  Q_OBJECT

public:
  explicit InputRoku(QObject* parent = nullptr) : InputBase(parent) { }
  bool initInput() override;
  const char* inputName() override { return "roku"; };

private:
  void handleRequest(const QHttpServerRequest& request, HttpResponse* response);
  void handleQueryApps(const QHttpServerRequest& request, HttpResponse* response);
  void handleKeyPress(const QHttpServerRequest& request, HttpResponse* response);
  void handleQueryDeviceInfo(const QHttpServerRequest& request, HttpResponse* response);

  HttpListener* m_server;
  QUdpSocket* m_ssdpSocket;

  void ssdpRead();
  void parseSSDPData(const QByteArray& data, const QHostAddress& sender, quint16 port);
  QByteArray getSSDPPacket(const QHostAddress& sender);
  void handleRootInfo(const QHttpServerRequest& request, HttpResponse* response);
};

#endif //PLEXMEDIAPLAYER_ROKUREMOTECOMPONENT_H
