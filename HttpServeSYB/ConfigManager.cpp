#include "ConfigManager.h"
#include <QJsonDocument>
#include <QJsonArray>
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
    if (m_initialized) {
        save();
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
    QMutexLocker locker(&m_mutex);

    if (m_initialized) {
        m_errorString = "Already initialized";
        return false;
    }

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
    case FormatRegistry:
        m_settings.reset(new QSettings(m_configPath, QSettings::NativeFormat));
        m_settings->setIniCodec("UTF-8");
        break;
    case FormatJson:
    case FormatXml:
        // 使用QVariantMap作为缓存
        break;
    }

    // 读取配置
    if (!readConfig()) {
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

    // 检查缓存
    if (m_cache.contains(key) && !m_cacheDirty) {
        return m_cache.value(key);
    }

    QVariant value;

    switch (m_format) {
    case FormatIni:
    case FormatRegistry:
        if (m_settings) {
            value = m_settings->value(key, defaultValue);
        }
        break;
    case FormatJson:
        // 支持点分隔的键路径，如"database.host"
        QStringList keys = key.split('.');
        QJsonObject obj = m_jsonData;

        for (int i = 0; i < keys.size(); ++i) {
            if (i == keys.size() - 1) {
                if (obj.contains(keys[i])) {
                    value = convertJsonValue(obj.value(keys[i]));
                }
            }
            else {
                if (obj.contains(keys[i]) && obj.value(keys[i]).isObject()) {
                    obj = obj.value(keys[i]).toObject();
                }
                else {
                    break;
                }
            }
        }

        if (!value.isValid()) {
            value = defaultValue;
        }
        break;
    case FormatXml:
        // XML实现类似JSON
        value = defaultValue;
        break;
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

    switch (m_format) {
    case FormatIni:
    case FormatRegistry:
        if (m_settings) {
            m_settings->setValue(key, value);
            success = true;
        }
        break;
    case FormatJson:
        // 支持嵌套键路径
        QStringList keys = key.split('.');
        if (keys.isEmpty()) {
            success = false;
            break;
        }

        QJsonObject* obj = &m_jsonData;
        for (int i = 0; i < keys.size() - 1; ++i) {
            if (!obj->contains(keys[i]) || !(*obj)[keys[i]].isObject()) {
                (*obj)[keys[i]] = QJsonObject();
            }
            obj = &(*obj)[keys[i]].toObject();
        }

        (*obj)[keys.last()] = convertToJsonValue(value);
        success = true;
        break;
    case FormatXml:
        // XML实现
        success = true;
        break;
    }

    if (success) {
        m_cache[key] = value;
        m_cacheDirty = true;
        emit configChanged(key, value);
    }

    return success;
}

void ConfigManager::beginGroup(const QString& prefix)
{
    if (m_settings && (m_format == FormatIni || m_format == FormatRegistry)) {
        m_settings->beginGroup(prefix);
    }
    // JSON/XML格式需要手动处理前缀
}

void ConfigManager::endGroup()
{
    if (m_settings && (m_format == FormatIni || m_format == FormatRegistry)) {
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
        if (!setValue(it.key(), it.value())) {
            allSuccess = false;
        }
    }

    if (allSuccess && !values.isEmpty()) {
        emit configChanged("batch_update", QVariant(values));
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
    case FormatRegistry:
        if (m_settings) {
            QStringList keys = m_settings->allKeys();
            for (const QString& key : keys) {
                allValues[key] = m_settings->value(key);
            }
        }
        break;
    case FormatJson:
        // 递归遍历JSON对象
        QQueue<QPair<QString, QJsonObject>> queue;
        queue.enqueue(qMakePair(QString(), m_jsonData));

        while (!queue.isEmpty()) {
            auto current = queue.dequeue();
            QString prefix = current.first;
            QJsonObject obj = current.second;

            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                QString fullKey = prefix.isEmpty() ? it.key() : prefix + "." + it.key();

                if (it.value().isObject()) {
                    queue.enqueue(qMakePair(fullKey, it.value().toObject()));
                }
                else {
                    allValues[fullKey] = convertJsonValue(it.value());
                }
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
    if (success) {
        m_cacheDirty = false;
        emit configSaved();
    }
    else {
        m_errorString = QString("Failed to save config to: %1").arg(m_configPath);
    }

    return success;
}

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
        emit configReloaded();
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

ConfigManager::ConfigFormat ConfigManager::getFormat() const
{
    return m_format;
}

bool ConfigManager::isInitialized() const
{
    return m_initialized;
}

QString ConfigManager::getErrorString() const
{
    return m_errorString;
}

QString ConfigManager::getString(const QString& key, const QString& defaultValue) const
{
    return getValue(key, defaultValue).toString();
}

int ConfigManager::getInt(const QString& key, int defaultValue) const
{
    return getValue(key, defaultValue).toInt();
}

double ConfigManager::getDouble(const QString& key, double defaultValue) const
{
    return getValue(key, defaultValue).toDouble();
}

bool ConfigManager::getBool(const QString& key, bool defaultValue) const
{
    QVariant value = getValue(key, defaultValue);
    if (value.type() == QVariant::Bool) {
        return value.toBool();
    }

    // 也支持字符串形式的bool
    QString str = value.toString().toLower();
    return (str == "true" || str == "1" || str == "yes" || str == "on");
}

QStringList ConfigManager::getStringList(const QString& key, const QStringList& defaultValue) const
{
    QVariant value = getValue(key, defaultValue);
    if (value.type() == QVariant::StringList) {
        return value.toStringList();
    }
    else if (value.type() == QVariant::String) {
        return value.toString().split(',', Qt::SkipEmptyParts);
    }
    return defaultValue;
}

int ConfigManager::getIntInRange(const QString& key, int min, int max, int defaultValue) const
{
    int value = getInt(key, defaultValue);
    return qBound(min, value, max);
}

double ConfigManager::getDoubleInRange(const QString& key, double min, double max, double defaultValue) const
{
    double value = getDouble(key, defaultValue);
    return qBound(min, value, max);
}

bool ConfigManager::readConfig()
{
    switch (m_format) {
    case FormatIni:
    case FormatRegistry:
        return m_settings && m_settings->status() == QSettings::NoError;
    case FormatJson:
        return readJson();
    case FormatXml:
        return readXml();
    }
    return false;
}

bool ConfigManager::writeConfig()
{
    switch (m_format) {
    case FormatIni:
    case FormatRegistry:
        if (m_settings) {
            m_settings->sync();
            return m_settings->status() == QSettings::NoError;
        }
        return false;
    case FormatJson:
        return writeJson();
    case FormatXml:
        return writeXml();
    }
    return false;
}

bool ConfigManager::readJson()
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 文件不存在，创建空配置
        m_jsonData = QJsonObject();
        return true;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        m_errorString = QString("JSON parse error: %1").arg(error.errorString());
        return false;
    }

    if (!doc.isObject()) {
        m_errorString = "JSON root is not an object";
        return false;
    }

    m_jsonData = doc.object();
    return true;
}

bool ConfigManager::writeJson()
{
    QJsonDocument doc(m_jsonData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        m_errorString = QString("Cannot open file for writing: %1").arg(m_configPath);
        return false;
    }

    qint64 written = file.write(jsonData);
    file.close();

    return written == jsonData.size();
}

bool ConfigManager::readXml()
{
    // XML实现类似JSON
    QFile file(m_configPath);
    if (!file.exists()) {
        return true; // 文件不存在不算错误
    }

    // 简化的XML读取
    return true;
}

bool ConfigManager::writeXml()
{
    // XML实现
    return true;
}

QString ConfigManager::getDefaultConfigPath() const
{
    QString appName = QCoreApplication::applicationName();
    if (appName.isEmpty()) {
        appName = "application";
    }

    QString fileName = appName + ".ini";

    // 优先使用应用程序目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString appDirPath = QDir(appDir).filePath(fileName);

    // 如果可写，使用应用程序目录
    QFileInfo appDirInfo(appDir);
    if (appDirInfo.isWritable()) {
        return appDirPath;
    }

    // 否则使用用户配置目录
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return dir.filePath(fileName);
}

QString ConfigManager::getBackupFileName() const
{
    QString baseName = QFileInfo(m_configPath).completeBaseName();
    QString suffix = QFileInfo(m_configPath).suffix();
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

    QString backupName = QString("%1_%2.%3").arg(baseName, timestamp, suffix);
    QString backupDir = QFileInfo(m_configPath).absolutePath() + "/backups";

    QDir dir(backupDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    return dir.filePath(backupName);
}

bool ConfigManager::ensureConfigDirectory() const
{
    QString configDir = QFileInfo(m_configPath).absolutePath();
    QDir dir(configDir);

    if (!dir.exists()) {
        return dir.mkpath(".");
    }

    return true;
}

QVariant ConfigManager::convertJsonValue(const QJsonValue& jsonValue) const
{
    switch (jsonValue.type()) {
    case QJsonValue::Bool:
        return jsonValue.toBool();
    case QJsonValue::Double:
        return jsonValue.toDouble();
    case QJsonValue::String:
        return jsonValue.toString();
    case QJsonValue::Array: {
        QJsonArray array = jsonValue.toArray();
        QVariantList list;
        for (const QJsonValue& val : array) {
            list.append(convertJsonValue(val));
        }
        return list;
    }
    case QJsonValue::Object:
        return QVariant::fromValue(jsonValue.toObject());
    case QJsonValue::Null:
    default:
        return QVariant();
    }
}

QJsonValue ConfigManager::convertToJsonValue(const QVariant& variant) const
{
    switch (variant.type()) {
    case QVariant::Bool:
        return QJsonValue(variant.toBool());
    case QVariant::Int:
    case QVariant::UInt:
    case QVariant::LongLong:
    case QVariant::ULongLong:
        return QJsonValue(variant.toLongLong());
    case QVariant::Double:
        return QJsonValue(variant.toDouble());
    case QVariant::String:
        return QJsonValue(variant.toString());
    case QVariant::StringList:
    case QVariant::List: {
        QVariantList list = variant.toList();
        QJsonArray array;
        for (const QVariant& item : list) {
            array.append(convertToJsonValue(item));
        }
        return array;
    }
    case QVariant::Map: {
        QVariantMap map = variant.toMap();
        QJsonObject obj;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            obj[it.key()] = convertToJsonValue(it.value());
        }
        return obj;
    }
    default:
        if (variant.canConvert<QJsonObject>()) {
            return variant.value<QJsonObject>();
        }
        return QJsonValue(QJsonValue::Null);
    }
}