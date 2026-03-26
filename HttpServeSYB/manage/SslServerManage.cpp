#include "SslServerManage.h"
#include "logger/Logger.h"
#include <QSslKey>

SslServerManage::SslServerManage(const ServerConfig config, QObject* parent)
	: AbstractManage(config, parent)
{
}

SslServerManage::~SslServerManage()
{
	stop();
}

bool SslServerManage::listen(
    const QHostAddress& address,
    quint16 port,
    const QString& crtFilePath,
    const QString& keyFilePath,
    const QList<QPair<QString, bool>>& caFileList // [ { filePath, isPem } ]
)
{
    m_listenAddress = address;
    m_listenPort = port;

    // 读取服务器应用证书和密钥文件,这些文件存储在服务端
    QFile fileForCrt(crtFilePath);//服务器应用证书
    if (!fileForCrt.open(QIODevice::ReadOnly)) {
        Logger::log(LogLevel::Error, QString("Cannot open certificate file: %1").arg(crtFilePath));
        return false;
    }

    QFile fileForKey(keyFilePath);//密钥
    if (!fileForKey.open(QIODevice::ReadOnly)) {
        Logger::log(LogLevel::Error, QString("Cannot open key file: %1").arg(keyFilePath));
        return false;
    }

    //加载证书，使用pem的格式，用于验证
    QSslCertificate sslCertificate(fileForCrt.readAll(), QSsl::Pem);
    //加载密钥，使用Rsa算法，pem文件格式，用于验证
    QSslKey sslKey(fileForKey.readAll(), QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey);

    fileForCrt.close();
    fileForKey.close();

    //验证证书是否有效
    if (sslCertificate.isNull()) {
        Logger::log(LogLevel::Error, "Invalid SSL certificate");
        return false;
    }

    //验证密钥是否有效
    if (sslKey.isNull()) {
        Logger::log(LogLevel::Error, "Invalid SSL key");
        return false;
    }

    /*
        有些证书需要中间CA证书来建立信任完整链
        缺少中间证书会导致验证失败
        caCertificates 应包含根证书和所有中间证书
    */
    // 读取CA证书
    QList<QSslCertificate> caCertificates;
    for (const auto& caFile : caFileList) 
    {
        QFile fileForCa(caFile.first);
        if (!fileForCa.open(QIODevice::ReadOnly)) 
        {
            Logger::log(LogLevel::Error, QString("Cannot open CA file: %1").arg(caFile.first));
            return false;
        }
        caCertificates.push_back(QSslCertificate(fileForCa.readAll(),
            caFile.second ? QSsl::Pem : QSsl::Der));
    }

    m_sslConfiguration = std::make_shared<QSslConfiguration>();
    m_sslConfiguration->setPeerVerifyMode(QSslSocket::VerifyPeer);//设置证书验证
    m_sslConfiguration->setLocalCertificate(sslCertificate);//设置服务器应用证书
    m_sslConfiguration->setPrivateKey(sslKey);//设置私钥
    m_sslConfiguration->setProtocol(QSsl::TlsV1_2OrLater);//设置协议级别

    // 效果：只有以下证书会被信任：
    // 1. 被 caCertificates 中任一CA直接签名
    // 2. 或被这些CA的中间CA签名
    m_sslConfiguration->setCaCertificates(caCertificates);//设置ca证书链

    return start();
}

bool SslServerManage::isRunning()
{
    QMutexLocker locker(&m_mutex);
    return !m_tcpServer.isNull();
}

bool SslServerManage::OnStart()
{
    QMutexLocker locker(&m_mutex);

    //创建ssl服务器，并指定有新连接到来时触发的lambda表达式函数
    m_tcpServer = new SslServerHelper([this](qintptr socketDescriptor)
        {
        auto sslSocket = new QSslSocket();
        //应用预设的 SSL 配置（包含证书、私钥、协议版本等）
        sslSocket->setSslConfiguration(*m_sslConfiguration);

        //encrypted信号会在ssl加密完成后触发
        // 内部流程：
        // 1. 等待客户端发送 ClientHello
        // 2. 发送 ServerHello + 证书
        // 3. 完成密钥交换
        // 4. 握手完成 → 触发 encrypted()
        connect(sslSocket, &QSslSocket::encrypted, [this, sslSocket]() 
            {
                NewSession(std::make_shared<Session>(
                    std::shared_ptr<QSslSocket>(sslSocket, [](QSslSocket*) {}),
                    m_config
                ));
            });

        //捕获ssl的错误列表，记录日志
        connect(sslSocket, qOverload<const QList<QSslError> &>(&QSslSocket::sslErrors),//这个qOverload是为了选择特定类型的重载信号函数
            [sslSocket](const QList<QSslError>& errors) 
            {
                for (const auto& error : errors) {
                    Logger::log(LogLevel::Warning,
                        QString("SSL error: %1").arg(error.errorString()));
                }
            });

        /*
        内部操作：
        1. 接管已有的 TCP 连接
        2. 设置 QSslSocket 的内部套接字描述符
        3. TCP连接就绪，等待SSL握手
        */
        sslSocket->setSocketDescriptor(socketDescriptor);

        /*
        内部操作：
        1. 切换到 SSL 服务器模式
        2. 等待客户端发送 ClientHello
        3. 收到 ClientHello 后，发送 ServerHello + 证书
        4. 进行密钥交换
        5. 完成握手，触发 encrypted() 信号
        */
        sslSocket->startServerEncryption();
        });

    //开始监听
    if (!m_tcpServer->listen(m_listenAddress, m_listenPort)) 
    {
        Logger::log(LogLevel::Error,
            QString("Failed to start SSL server on %1:%2: %3")
            .arg(m_listenAddress.toString())
            .arg(m_listenPort)
            .arg(m_tcpServer->errorString()));
        delete m_tcpServer.data();
        m_tcpServer.clear();
        return false;
    }

    Logger::log(LogLevel::Info,
        QString("SSL server listening on %1:%2")
        .arg(m_listenAddress.toString())
        .arg(m_listenPort));
    return true;
}

//关闭连接
bool SslServerManage::OnFinish()
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

