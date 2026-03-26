#pragma once
#include <memory>
#include <QThreadPool>
#include <QMutex>
#include <QSet>
#include <QSemaphore>
#include <QtConcurrent>

/**
    public.h文件记录一些公共的枚举和结构体
*/

//#define QT_NO_SSL true
// 错误码枚举
enum class ErrorCode
{
    Success = 0,
    InvalidRequest = 1,
    SessionExpired = 2,
    IODeviceError = 3,
    FileNotFound = 4,
    BufferError = 5,
    SSLConfigurationError = 6
};

// 日志级别
enum class LogLevel
{
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

//服务配置信息结构体
typedef struct ServerConfig
{
    int maxThreads{ 4 };                        // 最大线程数    
    int sessionTimeoutMs{ 30000 };              // 会话超时时间(30秒)    
    qint64 maxRequestSize{ 10 * 1024 * 1024 };  // 最大请求大小(10MB)    
    bool enableCompression{ true };             // 启用压缩(预留)    
    bool enableCors{ true };                    // 启用CORS跨域    
    LogLevel logLevel{ LogLevel::Info };        // 日志级别    
    int maxConnections{ 1000 };                 // 最大连接数
}ServerConfig;