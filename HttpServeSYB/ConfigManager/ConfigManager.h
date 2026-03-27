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
    };
    //单例销毁
    static void destroyInstance();
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
    bool reload();//清空缓存
    //备份配置文件
    bool backup(const QString& backupPath = QString()) const;
    //备份当前配置，删除当前配置，用历史备份还原为当前配置
    bool restore(const QString& backupPath);

    // 工具函数
    QString getConfigPath() const;
    bool isInitialized() const;
    QString getErrorString() const;

protected:
    explicit ConfigManager(QObject* parent = nullptr);
    ~ConfigManager();

    // 不同格式的读写实现
    virtual bool readConfig();
    virtual bool writeConfig();


private:
    // 单例实例
    static ConfigManager* m_instance;
    static QMutex m_mutex;

    // 配置数据
    std::unique_ptr<QSettings> m_settings;
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
};



