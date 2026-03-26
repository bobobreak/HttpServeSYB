#pragma once

#include <QObject>
#include <QSettings>
#include <QJsonObject>
#include <QVariant>
#include <QFile>
#include <QDir>
#include <QMutex>
#include <memory>

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    enum ConfigFormat {
        FormatIni,      // INI格式
        FormatJson,     // JSON格式
        FormatXml,      // XML格式
        FormatRegistry  // Windows注册表 (仅Windows)
    };

    // 单例模式获取实例
    static ConfigManager* instance();

    // 初始化配置文件
    bool initialize(const QString& configPath = QString(),
        ConfigFormat format = FormatIni);

    // 基本读写操作
    QVariant getValue(const QString& key,
        const QVariant& defaultValue = QVariant()) const;
    bool setValue(const QString& key, const QVariant& value);

    // 分组操作
    void beginGroup(const QString& prefix);
    void endGroup();

    // 批量操作
    bool setValues(const QVariantMap& values);
    QVariantMap getAllValues() const;

    // 文件操作
    bool save();
    bool reload();
    bool backup(const QString& backupPath = QString()) const;
    bool restore(const QString& backupPath);

    // 工具函数
    QString getConfigPath() const;
    ConfigFormat getFormat() const;
    bool isInitialized() const;
    QString getErrorString() const;

    // 类型安全的读取方法
    template<typename T>
    T getValueAs(const QString& key, const T& defaultValue = T()) const;

    QString getString(const QString& key, const QString& defaultValue = "") const;
    int getInt(const QString& key, int defaultValue = 0) const;
    double getDouble(const QString& key, double defaultValue = 0.0) const;
    bool getBool(const QString& key, bool defaultValue = false) const;
    QStringList getStringList(const QString& key,
        const QStringList& defaultValue = QStringList()) const;

    // 带验证的读取
    int getIntInRange(const QString& key, int min, int max, int defaultValue) const;
    double getDoubleInRange(const QString& key, double min, double max,
        double defaultValue) const;

    // 监听变化信号
signals:
    void configChanged(const QString& key, const QVariant& value);
    void configReloaded();
    void configSaved();
    void errorOccurred(const QString& error);

protected:
    explicit ConfigManager(QObject* parent = nullptr);
    ~ConfigManager();

    // 不同格式的读写实现
    virtual bool readConfig();
    virtual bool writeConfig();

    // 不同格式的解析
    bool readIni();
    bool writeIni();
    bool readJson();
    bool writeJson();
    bool readXml();
    bool writeXml();

private:
    // 单例实例
    static ConfigManager* m_instance;
    static QMutex m_mutex;

    // 配置数据
    std::unique_ptr<QSettings> m_settings;
    QJsonObject m_jsonData;
    QString m_configPath;
    ConfigFormat m_format;
    bool m_initialized;
    QString m_errorString;

    // 缓存
    mutable QVariantMap m_cache;
    mutable bool m_cacheDirty;//缓存数据脏了没

    // 辅助函数
    QString getDefaultConfigPath() const;
    QString getBackupFileName() const;
    bool ensureConfigDirectory() const;
    QVariant convertJsonValue(const QJsonValue& jsonValue) const;
    QJsonValue convertToJsonValue(const QVariant& variant) const;
};

// 模板实现
template<typename T>
T ConfigManager::getValueAs(const QString& key, const T& defaultValue) const
{
    QVariant value = getValue(key, QVariant::fromValue(defaultValue));
    if (value.canConvert<T>()) {
        return value.value<T>();
    }
    return defaultValue;
}


