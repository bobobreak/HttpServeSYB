#pragma once

#include <QObject>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include "AbstractManage.h"

class TcpServerManage  : public AbstractManage
{
    Q_OBJECT
    Q_DISABLE_COPY(TcpServerManage)

public:
	TcpServerManage(const ServerConfig config = {}, QObject* parent = nullptr);
	virtual ~TcpServerManage();

    bool listen(const QHostAddress& address = QHostAddress::Any, quint16 port = 0);

private:
    bool isRunning() override;
    bool OnStart() override;
    bool OnFinish() override;

private:
    QPointer<QTcpServer> m_tcpServer;
    QHostAddress m_listenAddress = QHostAddress::Any;
    quint16 m_listenPort = 0;//自动选择端口

};

