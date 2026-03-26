#pragma once

#include "TcpServerManage.h"
#include "SslServerManage.h"
#include "LocalServerManage.h"

class mainServerManage
{
public:
    mainServerManage(const ServerConfig& config = {}) : m_config(config) {}

    bool startTcpServer(const QHostAddress& address = QHostAddress::Any, quint16 port = 8080) 
    {
        m_tcpManage = std::make_unique<TcpServerManage>(m_config);
        return m_tcpManage->listen(address, port);
    }

#ifndef QT_NO_SSL
    bool startHttpsServer(const QHostAddress& address = QHostAddress::Any,
                            quint16 port = 8443,
                            const QString& crtPath = "",
                            const QString& keyPath = "") 
    {
        m_sslManage = std::make_unique<SslServerManage>(m_config);
        return m_sslManage->listen(address, port, crtPath, keyPath);
    }
#endif

    void setRequestHandler(std::function<void(const std::shared_ptr<Session>&)> handler) {
        if (m_tcpManage) m_tcpManage->SetHttpAcceptedCallback(handler);
#ifndef QT_NO_SSL
        if (m_sslManage) m_sslManage->SetHttpAcceptedCallback(handler);
#endif
    }

    void stop() {
        if (m_tcpManage) m_tcpManage->stop();
#ifndef QT_NO_SSL
        if (m_sslManage) m_sslManage->stop();
#endif
    }

private:
    ServerConfig m_config;
    std::unique_ptr<TcpServerManage> m_tcpManage;
#ifndef QT_NO_SSL
    std::unique_ptr<SslServerManage> m_sslManage;
#endif
};

