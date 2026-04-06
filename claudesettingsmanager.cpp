#include "claudesettingsmanager.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

ClaudeSettingsManager::ClaudeSettingsManager(QObject *parent)
    : QObject(parent)
{
    m_settingsPath = QDir::homePath() + "/.claude/settings.json";
}

QString ClaudeSettingsManager::settingsPath() const
{
    return m_settingsPath;
}

bool ClaudeSettingsManager::settingsFileExists() const
{
    return QFile::exists(m_settingsPath);
}

bool ClaudeSettingsManager::load()
{
    QFile file(m_settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage(QString("Claude settings not found: %1").arg(m_settingsPath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        emit logMessage(QString("Claude settings parse error: %1").arg(error.errorString()));
        return false;
    }

    m_rawJson = doc.object();

    // Parse env
    m_envVars.clear();
    if (m_rawJson.contains("env")) {
        QJsonObject envObj = m_rawJson["env"].toObject();
        for (auto it = envObj.begin(); it != envObj.end(); ++it) {
            m_envVars[it.key()] = it.value().toString();
        }
    }

    // Parse permissions
    m_permissionDefaultMode = "plan";
    m_permissionAllowList.clear();
    if (m_rawJson.contains("permissions")) {
        QJsonObject permObj = m_rawJson["permissions"].toObject();
        m_permissionDefaultMode = permObj.value("defaultMode").toString("plan");
        QJsonArray allowArr = permObj.value("allow").toArray();
        for (const QJsonValue &v : allowArr) {
            m_permissionAllowList.append(v.toString());
        }
    }

    // Parse model
    m_model = m_rawJson.value("model").toString();

    // Parse fastMode
    m_fastMode = m_rawJson.value("fastMode").toBool(false);

    // Parse skipDangerousModePermissionPrompt
    m_skipDangerousMode = m_rawJson.value("skipDangerousModePermissionPrompt").toBool(false);

    // Parse teammateMode
    m_teammateMode = m_rawJson.value("teammateMode").toString();

    // Parse agentSettings.teammateModel
    if (m_rawJson.contains("agentSettings")) {
        QJsonObject agentObj = m_rawJson["agentSettings"].toObject();
        m_teammateModel = agentObj.value("teammateModel").toString();
    }

    emit logMessage(QString("Claude settings loaded: %1 env vars").arg(m_envVars.size()));
    return true;
}

bool ClaudeSettingsManager::save()
{
    // Rebuild JSON from parsed fields, preserving unknown fields
    QJsonObject root = m_rawJson;

    // Write env
    QJsonObject envObj;
    for (auto it = m_envVars.begin(); it != m_envVars.end(); ++it) {
        envObj[it.key()] = it.value();
    }
    root["env"] = envObj;

    // Write permissions
    QJsonObject permObj;
    permObj["defaultMode"] = m_permissionDefaultMode;
    QJsonArray allowArr;
    for (const QString &item : m_permissionAllowList) {
        allowArr.append(item);
    }
    permObj["allow"] = allowArr;
    // Preserve deny list if exists
    if (m_rawJson.contains("permissions") && m_rawJson["permissions"].toObject().contains("deny")) {
        permObj["deny"] = m_rawJson["permissions"].toObject()["deny"];
    }
    root["permissions"] = permObj;

    // Write model
    if (!m_model.isEmpty()) {
        root["model"] = m_model;
    }

    // Write fastMode
    root["fastMode"] = m_fastMode;

    // Write skipDangerousModePermissionPrompt
    root["skipDangerousModePermissionPrompt"] = m_skipDangerousMode;

    // Write teammateMode
    if (!m_teammateMode.isEmpty()) {
        root["teammateMode"] = m_teammateMode;
    }

    // Write agentSettings
    QJsonObject agentObj;
    if (m_rawJson.contains("agentSettings")) {
        agentObj = m_rawJson["agentSettings"].toObject();
    }
    if (!m_teammateModel.isEmpty()) {
        agentObj["teammateModel"] = m_teammateModel;
    }
    if (!agentObj.isEmpty()) {
        root["agentSettings"] = agentObj;
    }

    // Ensure directory exists
    QDir dir(QDir::homePath() + "/.claude");
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonDocument doc(root);

    QFile file(m_settingsPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit logMessage(QString("Failed to save Claude settings: %1").arg(file.errorString()));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    m_rawJson = root;
    emit logMessage(QString("Claude settings saved to: %1").arg(m_settingsPath));
    emit settingsChanged();
    return true;
}

// Env var accessors
QMap<QString, QString> ClaudeSettingsManager::envVars() const { return m_envVars; }

void ClaudeSettingsManager::setEnvVar(const QString &key, const QString &value)
{
    m_envVars[key] = value;
}

void ClaudeSettingsManager::removeEnvVar(const QString &key)
{
    m_envVars.remove(key);
}

bool ClaudeSettingsManager::hasEnvVar(const QString &key) const
{
    return m_envVars.contains(key);
}

QString ClaudeSettingsManager::envVar(const QString &key) const
{
    return m_envVars.value(key);
}

// Permissions
QString ClaudeSettingsManager::permissionDefaultMode() const { return m_permissionDefaultMode; }
void ClaudeSettingsManager::setPermissionDefaultMode(const QString &mode) { m_permissionDefaultMode = mode; }
QStringList ClaudeSettingsManager::permissionAllowList() const { return m_permissionAllowList; }
void ClaudeSettingsManager::setPermissionAllowList(const QStringList &list) { m_permissionAllowList = list; }

// Model
QString ClaudeSettingsManager::model() const { return m_model; }
void ClaudeSettingsManager::setModel(const QString &model) { m_model = model; }

// Fast mode
bool ClaudeSettingsManager::fastMode() const { return m_fastMode; }
void ClaudeSettingsManager::setFastMode(bool enabled) { m_fastMode = enabled; }

// Skip dangerous mode
bool ClaudeSettingsManager::skipDangerousMode() const { return m_skipDangerousMode; }
void ClaudeSettingsManager::setSkipDangerousMode(bool enabled) { m_skipDangerousMode = enabled; }

// Teammate mode
QString ClaudeSettingsManager::teammateMode() const { return m_teammateMode; }
void ClaudeSettingsManager::setTeammateMode(const QString &mode) { m_teammateMode = mode; }

// Agent settings
QString ClaudeSettingsManager::teammateModel() const { return m_teammateModel; }
void ClaudeSettingsManager::setTeammateModel(const QString &model) { m_teammateModel = model; }

// Proxy URL helper
void ClaudeSettingsManager::setProxyUrl(const QString &url)
{
    setEnvVar("ANTHROPIC_BASE_URL", url);
}

// Mode detection
bool ClaudeSettingsManager::isApiMode() const
{
    return m_envVars.contains("ANTHROPIC_BASE_URL") || m_envVars.contains("ANTHROPIC_AUTH_TOKEN");
}

QString ClaudeSettingsManager::modeString() const
{
    return isApiMode() ? "API" : "Subscription";
}

QJsonObject ClaudeSettingsManager::rawJson() const { return m_rawJson; }
