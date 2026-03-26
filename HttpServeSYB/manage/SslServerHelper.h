#pragma once

#include <QTcpServer>

class SslServerHelper  : public QTcpServer
{
	Q_OBJECT

public:
    SslServerHelper(std::function<void(qintptr)> callback)
        : m_onIncomingConnectionCallback(std::move(callback)) {}

protected:
    //有新连接到达时触发
    void incomingConnection(qintptr socketDescriptor) override 
    {
        if (m_onIncomingConnectionCallback) 
        {
            m_onIncomingConnectionCallback(socketDescriptor);
        }
    }

private:
    std::function<void(qintptr)> m_onIncomingConnectionCallback;

};

