#include "LocalServerManage.h"
#include "logger/Logger.h"

LocalServerManage::LocalServerManage(const ServerConfig config, QObject* parent)
	: AbstractManage(config, parent)
{
}

LocalServerManage::~LocalServerManage()
{
	stop();
}

bool LocalServerManage::listen(const QString& name)
{
	m_listenName = name;
	return start();
}


bool LocalServerManage::isRunning()
{
	QMutexLocker locker(&m_mutex);
	return !m_localServer.isNull();
}

bool LocalServerManage::OnStart()
{
    QMutexLocker locker(&m_mutex);

    m_localServer = new QLocalServer();

    connect(m_localServer.data(), &QLocalServer::newConnection, [this]() 
        {
        auto socket = m_localServer->nextPendingConnection();
        if (socket) 
            {
                NewSession(std::make_shared<Session>(
                    std::shared_ptr<QLocalSocket>(socket, [](QLocalSocket*) {}),
                    m_config
                ));
            }
        });

    //QLocalServer监听本地有名管道，进行进程间通信
    if (!m_localServer->listen(m_listenName)) {
        Logger::log(LogLevel::Error,
            QString("Failed to start local server on %1: %2")
            .arg(m_listenName)
            .arg(m_localServer->errorString()));
        delete m_localServer.data();
        m_localServer.clear();
        return false;
    }

    Logger::log(LogLevel::Info,
        QString("Local server listening on %1").arg(m_listenName));
    return true;
}

bool LocalServerManage::OnFinish()
{
	QMutexLocker locker(&m_mutex);

	if (m_localServer)
	{
		m_localServer->close();
		delete m_localServer.data();
		m_localServer.clear();
	}
	return true;
}


