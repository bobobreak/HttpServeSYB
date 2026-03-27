#pragma once
#include <QObject>
#include <QDebug>
#include "public.h"
#include <QDateTime>

//日志类  运行时可动态过滤不同级别的日志
class Logger
{
public:
    //初始化日志，删除12个月之前的日志，检查是否存在当月的日志目录
    static void init();
    static void setLogLevel(LogLevel level);//设置日志级别

    //打印日志到日志文件中
    static void log(LogLevel level, const QString& message, const QObject* session = nullptr);

private:
	static QString levelToString(LogLevel level);//日志级别转字符串

    static LogLevel logLevel_;//用于控制日志级别，在初始化时在配置文件中读取
};

