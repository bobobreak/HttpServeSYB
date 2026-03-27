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

void Logger::init()
{
    auto funcDeleteTargetDir = [](QDir& targetDir, int number) -> QString
        {
            QFileInfoList fileinfoList = targetDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            QString targetAbsoluteFilePath;
            for (auto& it : fileinfoList)
            {
                //若存在小于number的目录，则直接删除
                bool isOk;
                int tempYear = it.fileName().toInt(&isOk);
                if (isOk && tempYear < number)
                {
                    //删除目录及子目录文件removeRecursively
                    QDir(it.absoluteFilePath()).removeRecursively();
                }
                else if (tempYear == number)
                {
                    targetAbsoluteFilePath = it.absoluteFilePath();
                    targetDir.setPath(targetAbsoluteFilePath);
                }
            }
            return targetAbsoluteFilePath;
        };
    //获取当前年月日
    QStringList dateList = QDate::currentDate().toString("yyyy:M:d").split(":");
    //删除12个月之前的日志文件及目录
    QDir logDir(QCoreApplication::applicationDirPath() + "/logger/");
    if (!logDir.exists())
    {
        logDir.mkdir(".");
        logDir.mkdir("./" + dateList.at(0));
        logDir.mkdir("./" + dateList.at(0) + "/" + dateList.at(1));
        return;
    }
    else
    {
        //找到需要删除的年和月
        int year = dateList.at(0).toInt() - 1;
        int moth = dateList.at(1).toInt();
        int day = dateList.at(2).toInt();
        //删除指定year前的目录，进入year目录
        if (!funcDeleteTargetDir(logDir, year).isEmpty())
        {
            //删除指定moth前的目录，进入moth目录
            if (!funcDeleteTargetDir(logDir, moth).isEmpty())
            {
                //删除指定日期之前的文件
                QFileInfoList fileinfoList = logDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (auto& it : fileinfoList)
                {
                    //若存在day前的文件，则直接删除
                    bool isOk;
                    int tempDay = it.fileName().section('_',0, 0).toInt(&isOk);
                    if (isOk && tempDay <= day)
                    {
                        //删除目录及子目录文件removeRecursively
                        QFile::remove(it.absoluteFilePath());
                    }
                }
            }
        }
        
        //创建当月目录文件
        logDir.setPath(QCoreApplication::applicationDirPath() + "/logger/" + QString::number(year + 1) + "/" + QString::number(moth) + "/");
        if (!logDir.exists())
        {
            logDir.mkpath(".");
        }


    }
    
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
        lggerDir.mkdir(".");
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