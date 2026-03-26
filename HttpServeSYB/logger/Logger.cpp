#include "Logger.h"
#include <QDate>
#include <QDir>
#include <QCoreApplication>

LogLevel Logger::logLevel_ = LogLevel::Debug;

//设置日志级别
void Logger::setLogLevel(LogLevel level)
{
    logLevel_ = level;
}

//打印日志
void Logger::log(LogLevel level, const QString& message, const QObject* session)
{
    if (level < logLevel_)
    {
        return;
    }

    QString logEntry = QString("[%1] [%2]: %3")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"))
        .arg(levelToString(level))
        .arg(message);

    if (session)
    {
        //为了追踪对话，标记出对话在内存中的地址
        logEntry += QString(" [Session:0x%1]").arg(reinterpret_cast<quintptr>(session), 0, 16);
    }


    QStringList logPathList = QDate::currentDate().toString("yyyy:MM:dd").split(":");
    QDir lggerDir(QCoreApplication::applicationDirPath() + "/logger/");
    if (!lggerDir.exists())
    {
        lggerDir.mkdir(lggerDir.absolutePath());
    }
    //这里可以扩展到日志打印在文件中
    switch (level) {
    case LogLevel::Debug:
    case LogLevel::Info:
        qDebug() << logEntry;
        break;
    case LogLevel::Warning:
        qWarning() << logEntry;
        break;
    case LogLevel::Error:
        qCritical() << logEntry;
        break;
    }
}

//日志级别转字符串
QString Logger::levelToString(LogLevel level) 
{
    switch (level) 
    {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "UNKNOWN";
}