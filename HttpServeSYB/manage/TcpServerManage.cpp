#include "TcpServerManage.h"
#include "logger/Logger.h"

TcpServerManage::TcpServerManage(const ServerConfig config, QObject* parent)
	: AbstractManage(config, parent)
{
}

TcpServerManage::~TcpServerManage()
{
	stop();
}

bool TcpServerManage::listen(const QHostAddress& address, quint16 port)
{
	//记录监听端口和地址，调用开始监听函数
	m_listenAddress = address;
	m_listenPort = port;
	return start();
}


bool TcpServerManage::isRunning()
{
	QMutexLocker locker(&m_mutex);
	return !m_tcpServer.isNull();
}

bool TcpServerManage::OnStart()
{
    QMutexLocker locker(&m_mutex);

    //TCP服务器
    m_tcpServer = new QTcpServer();

    connect(m_tcpServer.data(), &QTcpServer::newConnection, [this]() {
        auto socket = m_tcpServer->nextPendingConnection();
        if (socket) 
        {
            //对新的socket进行设置回调，统一放入m_availableSessions中进行管理
            NewSession(std::make_shared<Session>(
                std::shared_ptr<QTcpSocket>(socket, [](QTcpSocket*) { /* 不主动删除，由Session管理 */ }),
                m_config
            ));
        }
        });

    if (!m_tcpServer->listen(m_listenAddress, m_listenPort)) {
        Logger::log(LogLevel::Error,
            QString("Failed to start TCP server on %1:%2: %3")
            .arg(m_listenAddress.toString())
            .arg(m_listenPort)
            .arg(m_tcpServer->errorString()));
        delete m_tcpServer.data();
        m_tcpServer.clear();
        return false;
    }

    Logger::log(LogLevel::Info,
        QString("TCP server listening on %1:%2")
        .arg(m_listenAddress.toString())
        .arg(m_listenPort));
    return true;

}

bool TcpServerManage::OnFinish()
{
    QMutexLocker locker(&m_mutex);

    if (m_tcpServer) 
    {
        m_tcpServer->close();
        delete m_tcpServer.data();
        m_tcpServer.clear();
    }
    return true;
}



