#include "ConfigManager.h"
#include <QCoreApplication>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>

// 静态成员初始化
ConfigManager* ConfigManager::m_instance = nullptr;
QMutex ConfigManager::m_mutex;

ConfigManager::ConfigManager(QObject* parent)
    : QObject(parent)
    , m_settings(nullptr)
    , m_format(FormatIni)
    , m_initialized(false)
    , m_cacheDirty(false)
{
}

ConfigManager::~ConfigManager()
{
    if (m_initialized) 
    {
        save();
    }
}

void ConfigManager::destroyInstance()
{
    QMutexLocker locker(&m_mutex);
    if (m_instance) {
        delete m_instance; // 调用析构函数，触发 save()
        m_instance = nullptr;
    }
}

ConfigManager* ConfigManager::instance()
{
    //第一次判断是为了防止创建完成后再次锁定的性能开销
    if (!m_instance) 
    {
        QMutexLocker locker(&m_mutex);
        
        //第二次判断是为了防止重复创建
        if (!m_instance) 
        {
            m_instance = new ConfigManager();
        }
    }
    
    
    return m_instance;
}

bool ConfigManager::initialize(const QString& configPath, ConfigManager::ConfigFormat format)
{
    if (m_initialized)
    {
        m_errorString = "Already initialized";
        return false;
    }
    QMutexLocker locker(&m_mutex);

    // 确定配置文件路径
    if (configPath.isEmpty()) 
    {
        m_configPath = getDefaultConfigPath();
    }
    else 
    {
        m_configPath = configPath;
    }

    m_format = format;

    // 确保目录存在
    if (!ensureConfigDirectory()) 
    {
        m_errorString = QString("Cannot create config directory: %1").arg(QDir::toNativeSeparators(QFileInfo(m_configPath).absolutePath()));
        return false;
    }

    // 根据格式初始化
    switch (format) {
    case FormatIni:
    {
        //若m_configPath文件不存在，读取时会返回空值，持久化时若上级目录存在，则创建此文件
        m_settings.reset(new QSettings(m_configPath, QSettings::IniFormat));
    }
    }

    // 读取配置
    if (!readConfig()) 
    {
        m_errorString = QString("Failed to read config file: %1").arg(m_configPath);
        return false;
    }

    m_initialized = true;
    m_cacheDirty = false;
    return true;
}

QVariant ConfigManager::getValue(const QString& key, const QVariant& defaultValue) const
{
    if (!m_initialized) {
        qWarning() << "ConfigManager not initialized";
        return defaultValue;
    }

    QMutexLocker locker(&m_mutex);

    // 检查缓存，缓存有，则直接返回
    if (m_cache.contains(key) && !m_cacheDirty) {
        return m_cache.value(key);
    }

    QVariant value;

    switch (m_format)
    {
    case FormatIni:
    {
        if (m_settings) {
            value = m_settings->value(key, defaultValue);
        }
    }
    }

    // 更新缓存
    m_cache[key] = value;

    return value;
}

bool ConfigManager::setValue(const QString& key, const QVariant& value)
{
    if (!m_initialized) {
        m_errorString = "ConfigManager not initialized";
        return false;
    }

    QMutexLocker locker(&m_mutex);

    bool success = false;

    switch (m_format)
    {
    case FormatIni:
    {
        if (m_settings)
        {
            m_settings->setValue(key, value);
            success = true;
        }
        break;
    }
    }

    if (success) {
        //内存配置更新后缓存不可用，缓存数据和磁盘数据保持一致
        m_cache[key] = value;
        m_cacheDirty = true;
    }

    return success;
}

void ConfigManager::beginGroup(const QString& prefix)
{
    if (m_settings && (m_format == FormatIni)) 
    {
        m_settings->beginGroup(prefix);
    }
}

void ConfigManager::endGroup()
{
    if (m_settings && (m_format == FormatIni))
    {
        m_settings->endGroup();
    }
}

bool ConfigManager::setValues(const QVariantMap& values)
{
    if (!m_initialized) {
        return false;
    }

    QMutexLocker locker(&m_mutex);

    bool allSuccess = true;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!setValue(it.key(), it.value())) 
        {
            allSuccess = false;
        }
    }

    return allSuccess;
}

QVariantMap ConfigManager::getAllValues() const
{
    if (!m_initialized) {
        return QVariantMap();
    }

    QMutexLocker locker(&m_mutex);
    QVariantMap allValues;

    switch (m_format) {
    case FormatIni:
        if (m_settings) {
            QStringList keys = m_settings->allKeys();
            for (const QString& key : keys) {
                allValues[key] = m_settings->value(key);
            }
        }
        break;
    }

    return allValues;
}

bool ConfigManager::save()
{
    if (!m_initialized) {
        return false;
    }

    QMutexLocker locker(&m_mutex);

    bool success = writeConfig();
    if (success)
    {
        //写入磁盘后，缓存可用
        m_cacheDirty = false;
    }
    else {
        m_errorString = QString("Failed to save config to: %1").arg(m_configPath);
    }

    return success;
}

//清空缓存，之后读取时写入缓存
bool ConfigManager::reload()
{
    if (!m_initialized) {
        return false;
    }

    QMutexLocker locker(&m_mutex);

    bool success = readConfig();
    if (success) {
        m_cache.clear();
        m_cacheDirty = false;
    }
    else {
        m_errorString = QString("Failed to reload config from: %1").arg(m_configPath);
    }

    return success;
}

bool ConfigManager::backup(const QString& backupPath) const
{
    if (!m_initialized) {
        return false;
    }

    QString backupFile = backupPath;
    if (backupFile.isEmpty()) {
        backupFile = getBackupFileName();
    }

    return QFile::copy(m_configPath, backupFile);
}

//备份当前配置，删除当前配置，用历史备份还原为当前配置
bool ConfigManager::restore(const QString& backupPath)
{
    if (backupPath.isEmpty() || !QFile::exists(backupPath)) {
        m_errorString = "Backup file does not exist";
        return false;
    }

    // 先备份当前配置
    QString currentBackup = getBackupFileName();
    if (QFile::exists(m_configPath)) {
        QFile::copy(m_configPath, currentBackup);
    }

    // 恢复备份
    if (QFile::remove(m_configPath) && QFile::copy(backupPath, m_configPath)) {
        return reload();
    }

    return false;
}

QString ConfigManager::getConfigPath() const
{
    return m_configPath;
}

bool ConfigManager::isInitialized() const
{
    return m_initialized;
}

QString ConfigManager::getErrorString() const
{
    return m_errorString;
}

//检查配置是否可用
bool ConfigManager::readConfig()
{
    switch (m_format) 
    {
    case FormatIni:
        return m_settings && m_settings->status() == QSettings::NoError;
    }
    return false;
}

bool ConfigManager::writeConfig()
{
    switch (m_format) {
    case FormatIni:
        if (m_settings) {
            //写入磁盘，当没有带写入的数据时，sync不会创建新的文件
            m_settings->sync();
            return m_settings->status() == QSettings::NoError;
        }
        break;
    } 
    return false;
}

//获取完整的配置文件的目录及名称.../config/app.ini
QString ConfigManager::getDefaultConfigPath() const
{
    QString appName = QCoreApplication::applicationName();
    if (appName.isEmpty()) 
    {
        appName = "application";
    }

    //和可执行程序同名的配置文件名
    QString fileName = appName + ".ini";

    // 优先使用应用程序目录  .../config/app.ini
    QString appDir = QCoreApplication::applicationDirPath() + "/config/";//目录
    QString appDirPath = QDir(appDir).filePath(fileName);//完整目录文件名

    return appDirPath;
}

//获取一个基于当前时间的备份文件名（包括路径）的完整字串
QString ConfigManager::getBackupFileName() const
{
    /*
    QFileInfo info(""/home/user/docs/project.backup.tar.gz");
    // 获取不同部分
    info.absolutePath();        // 返回 "/home/user/docs"
    info.fileName();            // "project.backup.tar.gz"
    info.completeBaseName();    // "project.backup.tar"
    info.baseName();            // "project" （注意：baseName() 只到第一个点号）
    info.suffix();              // "gz"
    info.completeSuffix();      // "backup.tar.gz" （完整后缀）*/
    //处理备份文件的文件名
    QString baseName = QFileInfo(m_configPath).completeBaseName();//获取去掉最后一个点（包括）后面部分的文件名
    QString suffix = QFileInfo(m_configPath).suffix();//获取后缀名
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

    //拼接文件名和时间构成一个新的文件名
    QString backupName = QString("%1_%2.%3").arg(baseName, timestamp, suffix);
    QString backupDir = QFileInfo(m_configPath).absolutePath() + "/backups";

    QDir dir(backupDir);
    if (!dir.exists()) {
        dir.mkpath(".");//创建当前目录
    }

    //返回./backups/备份文件名称
    return dir.filePath(backupName);
}

//确保配置文件目录存在，不存在则创建
bool ConfigManager::ensureConfigDirectory() const
{
    QString configDir = QFileInfo(m_configPath).absolutePath();
    QDir dir(configDir);

    if (!dir.exists()) {
        return dir.mkpath(".");
    }

    return true;
}