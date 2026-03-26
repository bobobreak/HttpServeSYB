#include "AbstractManage.h"
#include "ConnectionManager.h"
#include "logger/Logger.h"

AbstractManage::AbstractManage(const ServerConfig config, QObject* parent)
	: m_config(config), QObject(parent)
{
	m_handleThreadPool = std::make_shared<QThreadPool>();
	m_serverThreadPool = std::make_shared<QThreadPool>();

	m_handleThreadPool->setMaxThreadCount(m_config.maxThreads);
	m_serverThreadPool->setMaxThreadCount(1);

	ConnectionManager::instance().setMaxConnections(m_config.maxConnections);

	Logger::setLogLevel(m_config.logLevel);
	
}

AbstractManage::~AbstractManage()
{
	stop();
}

bool AbstractManage::start()
{
	if (QThread::currentThread() != thread()) {
		Logger::log(LogLevel::Error, "Start called from wrong thread");
		return false;
	}

	if (isRunning()) {
		Logger::log(LogLevel::Warning, "Server already running");
		return false;
	}

	return StartServerThread();
}

void AbstractManage::stop()
{
	if (!isRunning())
	{
		Logger::log(LogLevel::Warning, "Server not running");
		return;
	}

	emit readyToClose();
	StopServerThread();
	StopHandleThread();
}

bool AbstractManage::StartServerThread()
{
	//用于控制多个线程访问共享资源的同步原语
	QSemaphore semaphore;
	bool success = false;

	QtConcurrent::run(m_serverThreadPool.get(), [&semaphore, &success, this]() {
		QEventLoop eventLoop;
		connect(this, &AbstractManage::readyToClose, &eventLoop, &QEventLoop::quit);

		success = this->OnStart();

		//用于线程同步，获取success值
		semaphore.release(1);

		if (success) {
			Logger::log(LogLevel::Info, "Server started successfully");
			eventLoop.exec();
			this->OnFinish();
			Logger::log(LogLevel::Info, "Server stopped");
		}
		});
	
	//由于semaphore初始值为0（默认构造），所以这里会阻塞，直到semaphore.release(1);被调用为止，/
	//用于正确获取success的值
	semaphore.acquire(1);
	return success;
}

void AbstractManage::StopServerThread()
{
	if (m_serverThreadPool) 
	{
		m_serverThreadPool->waitForDone();
	}
}

void AbstractManage::StopHandleThread()
{
	if (m_handleThreadPool) 
	{
		m_handleThreadPool->waitForDone();
	}
}

void AbstractManage::NewSession(const std::shared_ptr<Session>& session)
{
	if (!session)
	{
		return;
	}
		
	//这里设置的回调，在session接收到全部消息后触发
	session->setHandleAcceptedCallback([this](const std::shared_ptr<Session>& session_) 
	{
		HandleAccepted(session_);
	});

	//写入m_availableSessions中进行管理
	auto rawPtr = session.get();
	connect(rawPtr, &QObject::destroyed, [this, rawPtr]() 
	{
		QMutexLocker locker(&m_mutex);
		m_availableSessions.remove(rawPtr);
	});

	QMutexLocker locker(&m_mutex);
	m_availableSessions.insert(rawPtr);
}

void AbstractManage::HandleAccepted(const std::shared_ptr<Session>& session)
{
	//未设置http处理函数
	if (!m_httpAcceptedCallback) 
	{
		Logger::log(LogLevel::Error, "No HTTP accepted callback set");
		return;
	}

	QtConcurrent::run(m_handleThreadPool.get(), [this, session]() {
		try {
			m_httpAcceptedCallback(session);
		}
		catch (const std::exception& e) {
			Logger::log(LogLevel::Error,
				QString("Exception in request handler: %1").arg(e.what()),
				session.get());
		}
		catch (...) {
			Logger::log(LogLevel::Error,
				"Unknown exception in request handler",
				session.get());
		}
		});
}


