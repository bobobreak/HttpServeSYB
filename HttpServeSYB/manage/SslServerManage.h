#pragma once

#include <QObject>
#include <QHostAddress>
#include <QTcpServer>
#include <QSslConfiguration>
#include "AbstractManage.h"
#include "SslServerHelper.h"

class SslServerManage  : public AbstractManage
{
	Q_OBJECT
	Q_DISABLE_COPY(SslServerManage)

public:
	SslServerManage(const ServerConfig config = {}, QObject* parent = nullptr);
	virtual ~SslServerManage();

    bool listen(
        const QHostAddress& address = QHostAddress::Any,
        quint16 port = 0,
        const QString& crtFilePath = "",
        const QString& keyFilePath = "",
        const QList<QPair<QString, bool>>& caFileList = {} // [ { filePath, isPem } ]
    );

private:
    bool isRunning() override;
    bool OnStart() override;
    bool OnFinish() override;

private:
    QPointer<SslServerHelper> m_tcpServer;
    QHostAddress m_listenAddress = QHostAddress::Any;
    quint16 m_listenPort = 0;//自动选择端口
    std::shared_ptr<QSslConfiguration> m_sslConfiguration;//ssl的配置对象

};

