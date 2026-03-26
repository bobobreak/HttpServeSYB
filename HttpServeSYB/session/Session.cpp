#include "manage/ConnectionManager.h"
#include "logger/Logger.h"
#include "Session.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <QImage>


// 构造函数
Session::Session(const std::shared_ptr<QIODevice>& ioDevice, const ServerConfig& config)
    : ioDevice_(ioDevice), config_(config), timerForClose_(std::make_shared<QTimer>())
{
    if (!ioDevice_) {
        Logger::log(LogLevel::Error, "Session created with null IO device");
        return;
    }

    // 设置中断时间以及单次触发
    timerForClose_->setInterval(config_.sessionTimeoutMs);
    timerForClose_->setSingleShot(true);

    // 检查连接数限制
    if (!ConnectionManager::instance().canAcceptNewConnection()) {
        Logger::log(LogLevel::Warning, "Connection limit reached, rejecting new connection");
        safeDeleteLater();
        return;
    }

    ConnectionManager::instance().connectionEstablished();

    // 连接信号槽
    connect(ioDevice_.get(), &QIODevice::readyRead, this, &Session::onReadyRead);
    connect(ioDevice_.get(), &QIODevice::bytesWritten, this, &Session::onBytesWritten);
    connect(timerForClose_.get(), &QTimer::timeout, this, &Session::onTimeout);

    Logger::log(LogLevel::Debug, "New Session created", this);
}

Session::~Session()
{
    ConnectionManager::instance().connectionFinished();
    Logger::log(LogLevel::Debug, "Session destroyed", this);
}

/*
请求URL路径解析,去除URL内部询问子句
假设输入URL为："https://example.com//path//?filter%5Bname%5D=test&sort=asc"

处理过程：

移除?filter%5Bname%5D=test&sort=asc→ "https://example.com//path//"

移除尾部//→ "https://example.com//path"

开头是https://不是//，不处理

替换%5B为[，%5D为]（此处实际上已被第一步移除）

最终结果："https://example.com//path"
*/
QString Session::requestUrlPath() const
{
    static const QRegularExpression queryPattern("\\?.*$"); //找到末尾之前的第一个"？"字符
    static const QRegularExpression trailingSlash("/+$");   //找到末尾之前的所有"/"字符

    //构造由于被URL编码的特殊符号%5B:[      %5D: ]          %7B:{           %7D:}           %5E:^
    //eg:QString encoded = "example.com?filter%5Bname%5D=test"; 应用替换后: "example.com?filter[name]=test"
    static const QVector<QPair<QString, QString>> replacements = {
        {"%5B", "["}, {"%5D", "]"}, {"%7B", "{"}, {"%7D", "}"}, {"%5E", "^"}
    };

    QString result = requestUrl_;
    result.remove(queryPattern);//移除末尾开始第一个“？”字符之后的所有字符
    result.remove(trailingSlash);//移除末尾的所有“/”字符

    //若开头为两个//则删除一个/
    if (result.startsWith("//")) {
        result = result.mid(1);
    }

    for (const auto& rep : replacements) {
        result.replace(rep.first, rep.second);
    }

    return result;
}

QStringList Session::requestUrlPathSplitToList() const
{
    return requestUrlPath().split('/', QString::SkipEmptyParts);
}

/*
输入URL："https://example.com/api?filter%5Bname%5D=张三&sort=asc&page=1"

找到?位置，提取查询字符串："filter%5Bname%5D=张三&sort=asc&page=1"

URL解码："filter[name]=张三&sort=asc&page=1"

按&分割：

"filter[name]=张三"

"sort=asc"

"page=1"

按=分割为键值对：

filter[name]→ 张三

sort→ asc

page→ 1
*/
QMap<QString, QString> Session::requestUrlQuery() const
{
    const auto indexForQueryStart = requestUrl_.indexOf("?");
    if (indexForQueryStart < 0)
    {
        return {};
    }

    QMap<QString, QString> result;
    auto queryString = requestUrl_.mid(indexForQueryStart + 1);
    auto lines = QUrl::fromEncoded(queryString.toUtf8()).toString().split("&");
    for (auto line : lines)
    {
        auto indexOf = line.indexOf("=");
        if(indexOf > 0)
        { 
            result[line.mid(0, indexOf)] = line.mid(indexOf + 1); 
        }
    }
    return result;
}

// 文本响应
void Session::replyText(QString replyData, int httpStatusCode)
{

    JQHTTPSERVER_SESSION_PROTECTION("replyText");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this创建线程(ui线程)是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyText", Qt::QueuedConnection,
            Q_ARG(QString, std::move(replyData)), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_) 
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_TEXT_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg("text/plain;charset=UTF-8")
        .arg(QString::number(replyData.toUtf8().size()))
        .arg(securityHeaders)
        .arg(replyData)
        .toUtf8();

    waitWrittenByteCount_ = data.size();
    ioDevice_->write(data);

    Logger::log(LogLevel::Info,
        QString("Replied with text, status: %1, size: %2")
        .arg(httpStatusCode).arg(replyData.size()), this);
}

void Session::replyRedirects(const QUrl& targetUrl, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyRedirects");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyRedirects", Qt::QueuedConnection,
            Q_ARG(QUrl, targetUrl), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    const auto buffer = QString("<head><meta http-equiv=\"refresh\" content=\"0;URL=%1/\" /></head>")
        .arg(targetUrl.toString());

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_REDIRECTS_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg("text/html;charset=UTF-8")
        .arg(QString::number(buffer.toUtf8().size()))
        .arg(securityHeaders)
        .arg(buffer)
        .toUtf8();

    waitWrittenByteCount_ = data.size();
    ioDevice_->write(data);

    Logger::log(LogLevel::Info,
        QString("Redirected to: %1, status: %2")
        .arg(targetUrl.toString()).arg(httpStatusCode), this);
}

void Session::replyJsonObject(const QJsonObject& jsonObject, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyJsonObject");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyJsonObject", Qt::QueuedConnection,
            Q_ARG(QJsonObject, jsonObject), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    //QJsonDocument::Compact 用于生成紧凑型的json字符串，没有空格，换行等
    const auto data = QJsonDocument(jsonObject).toJson(QJsonDocument::Compact);

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto writedata = REPLY_TEXT_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg("application/json;charset=UTF-8")
        .arg(QString::number(data.size()))
        .arg(securityHeaders)
        .arg(QString(data))
        .toUtf8();

    waitWrittenByteCount_ = writedata.size();
    ioDevice_->write(writedata);

    Logger::log(LogLevel::Info,
        QString("Replied with JSON object, status: %1, size: %2")
        .arg(httpStatusCode)
        .arg(data.size()),
        this);
}

void Session::replyJsonArray(const QJsonArray& jsonArray, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyJsonArray");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyJsonArray", Qt::QueuedConnection,
            Q_ARG(QJsonArray, jsonArray), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    //QJsonDocument::Compact 用于生成紧凑型的json字符串，没有空格，换行等
    const auto data = QJsonDocument(jsonArray).toJson(QJsonDocument::Compact);

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto writedata = REPLY_TEXT_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg("application/json;charset=UTF-8")
        .arg(QString::number(data.size()))
        .arg(securityHeaders)
        .arg(QString(data))
        .toUtf8();

    waitWrittenByteCount_ = writedata.size();
    ioDevice_->write(writedata);

    Logger::log(LogLevel::Info,
        QString("Replied with JSON array, status: %1, size: %2")
        .arg(httpStatusCode)
        .arg(data.size()),
        this);
}

void Session::replyFile(const QString& filePath, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyFile");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyFile", Qt::QueuedConnection,
            Q_ARG(QString, filePath), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    //打开对应的文件，在ioDevice_->write(data)时，触发信号，读取ioDeviceForReply_内容，发送
    ioDeviceForReply_ = std::make_shared<QFile>(filePath);
    auto file = std::dynamic_pointer_cast<QFile>(ioDeviceForReply_);

    if (!file || !file->open(QIODevice::ReadOnly))
    {
        Logger::log(LogLevel::Error, QString("Failed to open file: %1").arg(filePath), this);
        ioDeviceForReply_.reset();//引用计数-1
        safeDeleteLater();
        return;
    }

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_FILE_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg(file->fileName())
        .arg(QString::number(file->size()))
        .arg(securityHeaders)
        .toUtf8();

    waitWrittenByteCount_ = data.size() + file->size();
    ioDevice_->write(data);

    Logger::log(LogLevel::Info,
        QString("Replied with file: %1, status: %2, size: %3")
        .arg(filePath)
        .arg(httpStatusCode)
        .arg(file->size()),
        this);
}

//仅png类型
void Session::replyImage(const QImage& image, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyImage");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyImage", Qt::QueuedConnection,
            Q_ARG(QImage, image), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    auto buffer = std::make_shared<QBuffer>();
    if (!buffer->open(QIODevice::ReadWrite))
    {
        Logger::log(LogLevel::Error, "Failed to open buffer for image", this);
        safeDeleteLater();
        return;
    }
    if (!image.save(buffer.get(), "png"))
    {
        Logger::log(LogLevel::Error, "Failed to save image to buffer", this);
        safeDeleteLater();
        return;
    }

    ioDeviceForReply_ = buffer;
    ioDeviceForReply_->seek(0);

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_IMAGE_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg(QString::number(buffer->size()))
        .arg(securityHeaders)
        .toUtf8();

    waitWrittenByteCount_ = data.size() + buffer->size();
    ioDevice_->write(data);


    Logger::log(LogLevel::Info,
        QString("Replied with image, status: %1, size: %2")
        .arg(httpStatusCode)
        .arg(buffer->buffer().size()),
        this);
}

//图片类型，不止png
void Session::replyImage(const QString& imageFilePath, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyImage");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyImage", Qt::QueuedConnection,
            Q_ARG(QString, imageFilePath), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    QImage image(imageFilePath);
    auto buffer = std::make_shared<QBuffer>();
    if (!buffer->open(QIODevice::ReadWrite))
    {
        Logger::log(LogLevel::Error, "Failed to open buffer for imageFilePath", this);
        safeDeleteLater();
        return;
    }
    if (!image.save(buffer.get()))
    {
        Logger::log(LogLevel::Error, "Failed to save imageFilePath to buffer", this);
        safeDeleteLater();
        return;
    }

    ioDeviceForReply_ = buffer;
    ioDeviceForReply_->seek(0);

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_IMAGE_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg(QString::number(buffer->size()))
        .arg(securityHeaders)
        .toUtf8();

    waitWrittenByteCount_ = data.size() + buffer->size();
    ioDevice_->write(data);


    Logger::log(LogLevel::Info,
        QString("Replied with image file: %1, status: %2, size: %3")
        .arg(imageFilePath)
        .arg(httpStatusCode)
        .arg(buffer->size()),
        this);
}

void Session::replyBytes(QByteArray bytes, int httpStatusCode)
{
    JQHTTPSERVER_SESSION_PROTECTION("replyBytes");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyBytes", Qt::QueuedConnection,
            Q_ARG(QByteArray, bytes), Q_ARG(int, httpStatusCode));
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    auto buffer = std::make_shared<QBuffer>();
    buffer->setData(std::move(bytes));
    if (!buffer->open(QIODevice::ReadWrite))
    {
        Logger::log(LogLevel::Error, "Failed to open buffer for bytes", this);
        safeDeleteLater();
        return;
    }

    

    ioDeviceForReply_ = buffer;
    ioDeviceForReply_->seek(0);

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_BYTES_FORMAT
        .arg(QString::number(httpStatusCode))
        .arg(QString::number(buffer->size()))
        .arg(securityHeaders)
        .toUtf8();

    waitWrittenByteCount_ = data.size() + buffer->size();
    ioDevice_->write(data);


    Logger::log(LogLevel::Info,
        QString("Replied with bytes, status: %1, size: %2")
        .arg(httpStatusCode)
        .arg(buffer->size()),
        this);
}

void Session::replyOptions()
{
    JQHTTPSERVER_SESSION_PROTECTION("replyOptions");

    if (alreadyReply_)
    {
        Logger::log(LogLevel::Warning, "Already replied to this session", this);
        return;
    }

    //判断执行线程和this线程是否同一个线程，若不同，则执行该函数放在this线程中执行，避免了加锁
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "replyOptions", Qt::QueuedConnection);
        return;
    }

    alreadyReply_ = true;
    if (!ioDevice_)
    {
        Logger::log(LogLevel::Error, "IO device is null", this);
        safeDeleteLater();
        return;
    }

    QString securityHeaders;
    addSecurityHeaders(securityHeaders);

    const auto data = REPLY_OPTIONS_FORMAT
        .arg(securityHeaders)
        .toUtf8();

    waitWrittenByteCount_ = data.size();
    ioDevice_->write(data);


    Logger::log(LogLevel::Info, "Replied to OPTIONS request", this);
}


void Session::onReadyRead()
{
    if (timerForClose_->isActive())
    {
        timerForClose_->stop();
    }

    const auto data = ioDevice_->readAll();
    buffer_.append(data);
    if (buffer_.size() > config_.maxRequestSize)
    {
        Logger::log(LogLevel::Warning,
            QString("Request size exceeded limit"), this);
        replyText("Request too large", 413);
        return;
    }

    inspectionBufferSetup1();
    timerForClose_->start();
}

void Session::onBytesWritten(qint64 bytes)
{
    waitWrittenByteCount_ -= bytes;
    if (waitWrittenByteCount_ <= 0)
    {
        safeDeleteLater();
        return;
    }
    if (ioDeviceForReply_ && !ioDeviceForReply_->atEnd())
    {
        ioDevice_->write(ioDeviceForReply_->read(512 * 1024));//最大支持一次上传512k
    }

    if (timerForClose_->isActive())
    {
        timerForClose_->stop();
    }
    timerForClose_->start();
}
void Session::onTimeout()
{ 
    Logger::log(LogLevel::Info, "Session timeout, closing", this);
    safeDeleteLater();
}

void Session::safeDeleteLater()
{
    if (ioDevice_)
    {
        ioDevice_->close();
    }
    deleteLater();
}

void Session::addSecurityHeaders(QString& headers)
{
    /*作用：防止浏览器进行 MIME 类型嗅探
    详细说明：
    某些浏览器会尝试猜测（嗅探）未正确设置 Content-Type 的资源的 MIME 类型
    这可能导致安全漏洞，比如将文本文件当作可执行脚本处理
    nosniff告诉浏览器严格遵守服务器声明的 Content-Type，不进行猜测
    */
    headers += "X-Content-Type-Options: nosniff\r\n";

    /*
    作用：防止网站在 iframe 中嵌入
    详细说明：
    防御点击劫持（Clickjacking）攻击
    DENY表示完全禁止在任何网站中通过 iframe 嵌入此页面
    替代值：SAMEORIGIN（只允许同源网站嵌入），ALLOW-FROM uri（允许指定源嵌入）
    */
    headers += "X-Frame-Options: DENY\r\n";

    /*
    作用：启用浏览器内置的 XSS 过滤保护
    详细说明：
    1：启用 XSS 过滤
    mode=block：当检测到 XSS 攻击时，直接阻止页面加载，而不是尝试清理
    主要针对旧版浏览器（IE、旧版 Chrome/Edge）的反射型 XSS
    */
    headers += "X-XSS-Protection: 1; mode=block\r\n";
    if(config_.enableCors)
    {
        //允许任意域名的网页通过 JavaScript 访问此接口
        headers += "Access-Control-Allow-Origin: *\r\n";
        //允许的 HTTP 方法列表, 当浏览器发送预检请求（OPTIONS）时，服务器告知客户端允许的方法
        headers += "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n";
        // 允许客户端在跨域请求中携带的额外请求头 
        // Content - Type：允许发送 JSON、XML 等格式的数据 
        // Authorization：允许发送身份验证信息（如 Bearer Token）
        headers += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"; 
    }
}

/*
* 示例 buffer_ = 
GET /api/users HTTP/1.1\r\n
Host: example.com\r\n
User-Agent: Test\r\n
\r\n

示例2：// 第一次收到的数据
POST /api/data HTTP/1.1\r\n
Host: example.com\r\n
Content-Type: application/json\r\n
Content-Length: 20\r\n
\r\n
{"name": "John",

// 第二次收到的数据
"age": 30}

*/
void Session::inspectionBufferSetup1()
{
    if (!headerAcceptedFinish_) {
        while (true) {
            static QByteArray splitFlag("\r\n");
            auto splitFlagIndex = buffer_.indexOf(splitFlag);

            //用户请求内容出错
            if (splitFlagIndex == -1) {
                if (requestMethod_.isEmpty() && buffer_.size() > 4) {
                    Logger::log(LogLevel::Warning, "Invalid request format", this);
                    safeDeleteLater();
                }
                return;
            }

            //用户没有给出请求方法，数据异常
            if (requestMethod_.isEmpty() && splitFlagIndex == 0) {
                Logger::log(LogLevel::Warning, "Empty request method", this);
                safeDeleteLater();
                return;
            }

            if (requestMethod_.isEmpty()) {
                //这里通过空格切割找出方法，api和协议
                auto requestLineDatas = buffer_.mid(0, splitFlagIndex).split(' ');
                buffer_.remove(0, splitFlagIndex + 2);

                if (requestLineDatas.size() != 3) {
                    Logger::log(LogLevel::Warning, "Invalid request line", this);
                    safeDeleteLater();
                    return;
                }

                //方法，api和协议
                requestMethod_ = requestLineDatas.at(0);
                requestUrl_ = requestLineDatas.at(1);
                requestCrlf_ = requestLineDatas.at(2);

                static const QStringList allowedMethods = { "GET", "OPTIONS", "POST", "PUT" };
                if (!allowedMethods.contains(requestMethod_.toUpper())) {
                    Logger::log(LogLevel::Warning,
                        QString("Unsupported method: %1").arg(requestMethod_), this);
                    safeDeleteLater();
                    return;
                }

                //日志记录用户请求的方法和api接口
                Logger::log(LogLevel::Debug,
                    QString("Request: %1 %2").arg(requestMethod_).arg(requestUrl_), this);
            }
            else if (splitFlagIndex == 0) {
                //处理用户请求中的内容
                buffer_.remove(0, 2);
                headerAcceptedFinish_ = true;

                bool shouldProcess = false;
                if (requestMethod_.toUpper() == "GET" || requestMethod_.toUpper() == "OPTIONS") {
                    shouldProcess = true;
                }
                else if ((requestMethod_.toUpper() == "POST" || requestMethod_.toUpper() == "PUT")) {
                    shouldProcess = (contentLength_ > 0) ? (!buffer_.isEmpty()) : true;
                }

                //GET和OPTIONS方法进入，POST和PUT方法进入代表有数据没有接受完
                if (shouldProcess) {
                    inspectionBufferSetup2();
                }
            }
            else {
                //处理协议头中的关键字和值信息写入requestHeader_中
                auto index = buffer_.indexOf(':');
                if (index <= 0) {
                    Logger::log(LogLevel::Warning, "Invalid header format", this);
                    safeDeleteLater();
                    return;
                }

                //找到请求的地址key:Host
                auto headerData = buffer_.mid(0, splitFlagIndex);
                buffer_.remove(0, splitFlagIndex + 2);

                const auto key = headerData.mid(0, index);
                auto value = headerData.mid(index + 1).trimmed();

                requestHeader_[key] = value;

                //着重处理content-length字段
                if (key.toLower() == "content-length") {
                    contentLength_ = value.toLongLong();
                }
            }
        }
    }
    else {
        inspectionBufferSetup2();
    }
}

void Session::inspectionBufferSetup2()
{
    //将接收到的内容写入到requestBody_中
    requestBody_ += buffer_;
    buffer_.clear();

    if (!handleAcceptedCallback_) {
        Logger::log(LogLevel::Error, "No handle accepted callback set", this);
        safeDeleteLater();
        return;
    }

    //等待剩余内容接收
    if (contentLength_ != -1 && requestBody_.size() != contentLength_) 
    {
        Logger::log(LogLevel::Debug,
            QString("Request body received, size: %1").arg(requestBody_.size()), this);
        return;
    }
    else if (contentLength_ == -1)
    {
        Logger::log(LogLevel::Debug,
            QString("Not need receive request body method"), this);
    }


    //使用智能指针，但不想让这个指针释放this
    auto self = std::shared_ptr<Session>(this, [](Session*) {}); // 小心使用，确保生命周期
    handleAcceptedCallback_(self);
}

/*
状态转移图：
[初始状态]
    ↓
收到数据 → inspectionBufferSetup1()
    ↓
解析请求行 ←┐
    ↓       │
解析头部   │ while 循环
    ↓       │
遇到空行 → 标记 headerAcceptedFinish_ = true
    ↓
调用 inspectionBufferSetup2() → 可能返回等待
    ↓
后续数据 → 直接进入 else 分支
    ↓
调用 inspectionBufferSetup2() → 可能完成
*/