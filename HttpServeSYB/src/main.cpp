
#include "public.h"
#include "logger\Logger.h"

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	Logger::log(LogLevel::Info, "111");
	qDebug() << "hello world";

	system("pause");
	return 0;
}


/*
良好的架构与模块化设计：
框架采用了抽象基类（AbstractManage）​ 与具体实现类（TcpServerManage, SslServerManage, LocalServerManage）的设计模式，职责清晰，便于扩展新的协议（如UDP、WebSocket等）。
核心逻辑（服务器管理、会话处理、日志、连接控制）被分离到不同的类中，遵循了单一职责原则。

全面的功能支持：
多协议支持：核心类mainServerManage统一管理TCP、HTTPS（SSL）和本地套接字（LocalSocket）服务器的启动、停止和请求处理回调设置，为上层使用提供了简洁的接口。

完整的HTTP请求/响应处理：Session类实现了HTTP请求的解析（请求行、头部、正文），并提供了丰富的响应方法（文本、JSON、文件、图片、字节流、重定向、OPTIONS等）。

安全性考虑：支持SSL/TLS加密通信，并在HTTP响应中默认添加了多项安全头部（X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, CORS相关头部）。

可配置性：通过ServerConfig结构体集中管理服务器配置，如线程数、超时、请求大小、日志级别、最大连接数等，易于调整。

健壮性与可靠性：

线程安全：在多处使用了QMutex保护共享资源（如m_availableSessions列表），并使用QThreadPool管理连接处理线程，防止数据竞争。

资源与生命周期管理：使用智能指针（std::shared_ptr, std::unique_ptr, QPointer）管理对象生命周期，防止内存泄漏。ConnectionManager单例有效控制了系统的总连接数。

详细的日志系统：Logger类支持不同级别的日志记录，并可根据配置动态过滤，便于调试和监控。

错误处理：关键操作都有错误检查和日志记录（如文件打开失败、SSL证书无效、服务器监听失败等），并使用异常捕获机制处理请求处理过程中的未知异常。

会话超时与保活：通过QTimer实现会话超时自动清理，并在数据传输时重置定时器，防止资源被无用连接长期占用。*/