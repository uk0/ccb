#ifndef CLAUDESETTINGSMANAGER_H
#define CLAUDESETTINGSMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>

class ClaudeSettingsManager : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeSettingsManager(QObject *parent = nullptr);

    // Load/Save
    bool load();
    bool save();
    QString settingsPath() const;
    bool settingsFileExists() const;

    // Environment variables
    QMap<QString, QString> envVars() const;
    void setEnvVar(const QString &key, const QString &value);
    void removeEnvVar(const QString &key);
    bool hasEnvVar(const QString &key) const;
    QString envVar(const QString &key) const;

    // Permissions
    QString permissionDefaultMode() const;
    void setPermissionDefaultMode(const QString &mode);
    QStringList permissionAllowList() const;
    void setPermissionAllowList(const QStringList &list);

    // Model
    QString model() const;
    void setModel(const QString &model);

    // Fast mode
    bool fastMode() const;
    void setFastMode(bool enabled);

    // Skip dangerous mode permission prompt
    bool skipDangerousMode() const;
    void setSkipDangerousMode(bool enabled);

    // Teammate mode
    QString teammateMode() const;
    void setTeammateMode(const QString &mode);

    // Agent settings
    QString teammateModel() const;
    void setTeammateModel(const QString &model);

    // Update proxy URL in env
    void setProxyUrl(const QString &url);

    // Mode detection: API vs Subscription
    // API mode = has ANTHROPIC_BASE_URL or ANTHROPIC_AUTH_TOKEN in env
    bool isApiMode() const;
    QString modeString() const;  // "API" or "Subscription"

    // Get full JSON for unknown/extra fields preservation
    QJsonObject rawJson() const;

signals:
    void logMessage(const QString &message);
    void settingsChanged();

private:
    QString m_settingsPath;
    QJsonObject m_rawJson;  // Preserve unknown fields

    // Parsed fields
    QMap<QString, QString> m_envVars;
    QString m_permissionDefaultMode;
    QStringList m_permissionAllowList;
    QString m_model;
    bool m_fastMode = false;
    bool m_skipDangerousMode = false;
    QString m_teammateMode;
    QString m_teammateModel;
};

#endif // CLAUDESETTINGSMANAGER_H
