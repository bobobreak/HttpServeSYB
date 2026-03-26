#pragma once

#include <QObject>
#include "public.h"
#include "session/Session.h"

class AbstractManage  : public QObject
{
	Q_OBJECT
	Q_DISABLE_COPY(AbstractManage)

public:
	AbstractManage(const ServerConfig config = {}, QObject * parent = nullptr);
	virtual ~AbstractManage();

	inline void SetHttpAcceptedCallback(std::function<void(const std::shared_ptr<Session>&)>& callback)
	{
		m_httpAcceptedCallback = callback;
	}

	//获取处理线程池的对象
	inline std::shared_ptr<QThreadPool> HandleThreadPool() const
	{
		return m_handleThreadPool;
	}

	//获取链接线程池的对象
	inline std::shared_ptr<QThreadPool> ServerThreadPool() const
	{
		return m_serverThreadPool;
	}

	//获取配置信息
	inline ServerConfig Config() const
	{
		return m_config;
	}

	virtual bool isRunning() = 0;

public slots:
	//启动和关闭http服务的槽函数
	bool start();
	void stop();

signals:
	void readyToClose();

protected:
	virtual bool OnStart() = 0;
	virtual bool OnFinish() = 0;

	//启动服务器线程
	bool StartServerThread();
	//停止服务器线程
	void StopServerThread();
	//停止消息处理线程
	void StopHandleThread();

	//新会话到达
	void NewSession(const std::shared_ptr<Session>& session);

	//消息处理回调
	void HandleAccepted(const std::shared_ptr<Session>& session);

protected:
	//服务线程池和处理消息的线程池
	std::shared_ptr<QThreadPool> m_serverThreadPool;
	std::shared_ptr<QThreadPool> m_handleThreadPool;

	QMutex m_mutex;
	ServerConfig m_config;
	//处理消息的回调函数
	std::function<void(const std::shared_ptr<Session>&)> m_httpAcceptedCallback;

	//存储可以通信的会话
	QSet<Session*> m_availableSessions;

};

