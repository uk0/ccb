#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>

class BackendPool;

class ConfigManager : public QObject
{
    Q_OBJECT

public:
    explicit ConfigManager(QObject *parent = nullptr);

    void setConfigPath(const QString &path);
    QString configPath() const;

    bool load(BackendPool *pool);
    bool save(const BackendPool *pool);

    int listenPort() const;
    void setListenPort(int port);

    int retryCount() const;
    void setRetryCount(int count);

    int cooldownSeconds() const;
    void setCooldownSeconds(int seconds);

    int timeoutSeconds() const;
    void setTimeoutSeconds(int seconds);

    bool correctionEnabled() const;
    void setCorrectionEnabled(bool enabled);

    bool localTokenCount() const;
    void setLocalTokenCount(bool enabled);

    bool debugLog() const;
    void setDebugLog(bool enabled);

signals:
    void logMessage(const QString &message);

private:
    QString m_configPath;
    int m_listenPort = 8080;
    int m_retryCount = 3;
    int m_cooldownSeconds = 60;
    int m_timeoutSeconds = 300;  // Default 5 minutes for Claude Code
    bool m_correctionEnabled = true;  // Default enabled for empty 200 response correction
    bool m_localTokenCount = true;  // Default enabled for fast local token estimation
    bool m_debugLog = false;  // Default disabled for full request body logging
};

#endif // CONFIGMANAGER_H
