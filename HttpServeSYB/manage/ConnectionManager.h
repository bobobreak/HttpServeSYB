#pragma once
#include <iostream>
/// <summary>
/// 所有连接的管理类
/// </summary>
class ConnectionManager
{
public:
    // 删除拷贝操作
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

	bool canAcceptNewConnection() const;	// 检查是否可以接受新连接
	void connectionEstablished();			// 连接建立时调用
	void connectionFinished();				// 连接结束时调用
    void setMaxConnections(int max);        // 设置最大连接数
    int activeConnections() const;          // 返回正在使用的连接数

    static ConnectionManager& instance();   //单例函数入口

private:
    ConnectionManager() = default;
    
    std::atomic<int> activeConnections_{ 0 };		// 当前活跃连接数
    std::atomic<int> maxConnections_{ 1000 };		// 最大连接数限制

    static ConnectionManager m_instance;    //单例对象
};
