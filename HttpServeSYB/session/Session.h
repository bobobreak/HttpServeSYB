#pragma once
#include <QObject>
#include <QIODevice>
#include <memory>
#include <QMap>
#include <QTimer>
#include "public.h"

const QString REPLY_TEXT_FORMAT =
"HTTP/1.1 %1 OK\r\n"
"Content-Type: %2\r\n"
"Content-Length: %3\r\n"
"%4"
"\r\n"
"%5";

const QString REPLY_REDIRECTS_FORMAT =
"HTTP/1.1 %1 OK\r\n"
"Content-Type: %2\r\n"
"Content-Length: %3\r\n"
"%4"
"\r\n"
"%5";

const QString REPLY_FILE_FORMAT =
"HTTP/1.1 %1 OK\r\n"
"Content-Disposition: attachment;filename=%2\r\n"
"Content-Length: %3\r\n"
"%4"
"\r\n";

const QString REPLY_IMAGE_FORMAT =
"HTTP/1.1 %1\r\n"
"Content-Type: image/png\r\n"
"Content-Length: %2\r\n"
"%3"
"\r\n";

const QString REPLY_BYTES_FORMAT =
"HTTP/1.1 %1 OK\r\n"
"Content-Type: application/octet-stream\r\n"
"Content-Length: %2\r\n"
"%3"
"\r\n";

const QString REPLY_OPTIONS_FORMAT =
"HTTP/1.1 200 OK\r\n"
"Allow: OPTIONS, GET, POST, PUT, HEAD\r\n"
"Access-Control-Allow-Methods: OPTIONS, GET, POST, PUT, HEAD\r\n"
"Content-Length: 0\r\n"
"%2"
"\r\n";

#define JQHTTPSERVER_SESSION_PROTECTION(functionName, ...) \
    if (!this || contentLength_ < -1 || waitWrittenByteCount_ < -1) { \
        Logger::log(LogLevel::Error, \
            QString("JQHttpServer::Session::%1: current session this is null").arg(functionName)); \
        return __VA_ARGS__; \
    }

class Session : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Session)

public:
    Session(const std::shared_ptr<QIODevice>& ioDevice, const ServerConfig& config);
    ~Session();

    // 设置回调函数
    inline void setHandleAcceptedCallback(const std::function<void(const std::shared_ptr<Session>&)>& callback) 
    {
        handleAcceptedCallback_ = callback;
    }

    // 请求信息获取
    inline QString requestMethod() const 
    { 
        return requestMethod_; 
    }
    inline QString requestUrl() const 
    { 
        return requestUrl_; 
    }

    //获取信息头和信息体
    inline QMap<QString, QString> requestHeader() const 
    { 
        return requestHeader_; 
    }
    inline QByteArray requestBody() const 
    { 
        return requestBody_; 
    }

    //获取url的路径
    QString requestUrlPath() const;
    //切割URL路径到列表
    QStringList requestUrlPathSplitToList() const;

    QMap<QString, QString> requestUrlQuery() const;

public slots:
    // 响应方法
    void replyText(QString replyData, int httpStatusCode = 200);
    void replyRedirects(const QUrl& targetUrl, int httpStatusCode = 200);
    void replyJsonObject(const QJsonObject& jsonObject, int httpStatusCode = 200);
    void replyJsonArray(const QJsonArray& jsonArray, int httpStatusCode = 200);
    void replyFile(const QString& filePath, int httpStatusCode = 200);
    void replyImage(const QImage& image, int httpStatusCode = 200);
    void replyImage(const QString& imageFilePath, int httpStatusCode = 200);
    void replyBytes(QByteArray bytes, int httpStatusCode = 200);
    void replyOptions();

private slots:
    void onReadyRead();                 //读
    void onBytesWritten(qint64 bytes);  //写
    void onTimeout();                   //超时

private:
    void inspectionBufferSetup1();
    void inspectionBufferSetup2();
    void addSecurityHeaders(QString& headers);//增加防御机制的头
    void safeDeleteLater();

private:
    std::shared_ptr<QIODevice> ioDevice_;       //IO设备指针
    std::function<void(const std::shared_ptr<Session>&)> handleAcceptedCallback_;   //回调函数
    std::shared_ptr<QTimer> timerForClose_;     //超时关闭的时间指针
    ServerConfig config_;                       //会话配置

    QByteArray buffer_;//收取用户请求的容器
    QString requestMethod_;//请求头中的方法 get put ……
    QString requestUrl_;//请求头中的api
    QString requestCrlf_;//请求协议 http或https
    QMap<QString, QString> requestHeader_;//存放请求头的信息和值
    bool headerAcceptedFinish_ = false;
    qint64 contentLength_ = -1;//客户端请求内容的长度
    bool alreadyReply_ = false;
    QByteArray requestBody_;//接收post传递过来内容的缓存
    qint64 waitWrittenByteCount_ = 0;
    std::shared_ptr<QIODevice> ioDeviceForReply_;
};

