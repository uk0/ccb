#include "configmanager.h"
#include "backendpool.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>

ConfigManager::ConfigManager(QObject *parent)
    : QObject(parent)
{
    // Use QStandardPaths for proper macOS/Linux/Windows paths
    // macOS: ~/Library/Application Support/ccb/
    // Linux: ~/.local/share/ccb/
    // Windows: C:/Users/<USER>/AppData/Local/ccb/
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (configDir.isEmpty()) {
        // Fallback to home directory
        configDir = QDir::homePath() + "/.ccb";
    }
    QDir().mkpath(configDir);
    m_configPath = configDir + "/config.json";

    // Migrate config from old location if it exists
    QString oldConfigPath = QDir::homePath() + "/.config/ccb/config.json";
    if (!QFile::exists(m_configPath) && QFile::exists(oldConfigPath)) {
        if (QFile::copy(oldConfigPath, m_configPath)) {
            qDebug() << "Migrated config from" << oldConfigPath << "to" << m_configPath;
        }
    }
}

void ConfigManager::setConfigPath(const QString &path)
{
    m_configPath = path;
}

QString ConfigManager::configPath() const
{
    return m_configPath;
}

bool ConfigManager::load(BackendPool *pool)
{
    QFile file(m_configPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit logMessage(QString("Config file not found: %1, using defaults").arg(m_configPath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        emit logMessage(QString("Config parse error: %1").arg(error.errorString()));
        return false;
    }

    QJsonObject root = doc.object();

    m_listenPort = root.value("listenPort").toInt(8080);
    m_retryCount = root.value("retryCount").toInt(3);
    m_cooldownSeconds = root.value("cooldownSeconds").toInt(60);
    m_timeoutSeconds = root.value("timeoutSeconds").toInt(300);
    m_correctionEnabled = root.value("correctionEnabled").toBool(true);
    m_localTokenCount = root.value("localTokenCount").toBool(true);

    // Clear existing groups (except the default one)
    while (pool->getGroupCount() > 1) {
        pool->removeGroup(pool->getGroupCount() - 1);
    }

    // Check for new group-based format
    if (root.contains("groups")) {
        // New format with groups
        QJsonArray groupsArray = root.value("groups").toArray();

        // Clear the default group's URLs and Keys first
        pool->setCurrentGroup(0);
        while (!pool->getUrls().isEmpty()) {
            pool->removeUrl(0);
        }
        while (!pool->getKeys().isEmpty()) {
            pool->removeKey(0);
        }

        bool isFirstGroup = true;
        for (const QJsonValue &groupVal : groupsArray) {
            QJsonObject groupObj = groupVal.toObject();
            QString groupName = groupObj.value("name").toString("Unnamed Group");

            if (isFirstGroup) {
                // Rename the default group
                pool->renameGroup(0, groupName);
                isFirstGroup = false;
            } else {
                // Add new group
                pool->addGroup(groupName);
                pool->setCurrentGroup(pool->getGroupCount() - 1);
            }

            // Load URLs for this group
            QJsonArray urlsArray = groupObj.value("urls").toArray();
            for (const QJsonValue &val : urlsArray) {
                QJsonObject obj = val.toObject();
                QString url = obj.value("url").toString();
                bool enabled = obj.value("enabled").toBool(true);
                if (!url.isEmpty()) {
                    pool->addUrl(url, enabled);
                }
            }

            // Load Keys for this group
            QJsonArray keysArray = groupObj.value("keys").toArray();
            for (const QJsonValue &val : keysArray) {
                QJsonObject obj = val.toObject();
                QString key = obj.value("key").toString();
                QString label = obj.value("label").toString();
                bool enabled = obj.value("enabled").toBool(true);
                if (!key.isEmpty()) {
                    pool->addKey(key, label, enabled);
                }
            }

            // Load Model Mappings for this group
            QJsonArray mappingsArray = groupObj.value("modelMappings").toArray();
            for (const QJsonValue &val : mappingsArray) {
                QJsonObject obj = val.toObject();
                QString sourceModel = obj.value("sourceModel").toString();
                QString targetModel = obj.value("targetModel").toString();
                if (!sourceModel.isEmpty() && !targetModel.isEmpty()) {
                    pool->addModelMapping(sourceModel, targetModel);
                }
            }

            // Load OpenAI format setting
            bool useOpenAI = groupObj.value("useOpenAIFormat").toBool(false);
            pool->setOpenAIFormatEnabled(useOpenAI);
        }

        // Restore to first group
        pool->setCurrentGroup(0);

        // Load current group index
        int currentGroupIndex = root.value("currentGroupIndex").toInt(0);
        if (currentGroupIndex >= 0 && currentGroupIndex < pool->getGroupCount()) {
            pool->setCurrentGroup(currentGroupIndex);
        }
    } else {
        // Old format - migrate to default group "Anthropic Official"
        pool->setCurrentGroup(0);
        pool->renameGroup(0, "Anthropic Official");

        // Clear existing URLs and Keys
        while (!pool->getUrls().isEmpty()) {
            pool->removeUrl(0);
        }
        while (!pool->getKeys().isEmpty()) {
            pool->removeKey(0);
        }

        // Load URLs (old format)
        QJsonArray urlsArray = root.value("apiUrls").toArray();
        for (const QJsonValue &val : urlsArray) {
            QJsonObject obj = val.toObject();
            QString url = obj.value("url").toString();
            bool enabled = obj.value("enabled").toBool(true);
            if (!url.isEmpty()) {
                pool->addUrl(url, enabled);
            }
        }

        // Load Keys (old format)
        QJsonArray keysArray = root.value("apiKeys").toArray();
        for (const QJsonValue &val : keysArray) {
            QJsonObject obj = val.toObject();
            QString key = obj.value("key").toString();
            QString label = obj.value("label").toString();
            bool enabled = obj.value("enabled").toBool(true);
            if (!key.isEmpty()) {
                pool->addKey(key, label, enabled);
            }
        }

        emit logMessage("Migrated old config format to group-based format");
    }

    int totalUrls = 0, totalKeys = 0;
    for (int i = 0; i < pool->getGroupCount(); i++) {
        BackendGroup group = pool->getGroup(i);
        totalUrls += group.urls.size();
        totalKeys += group.keys.size();
    }

    emit logMessage(QString("Config loaded: %1 groups, %2 URLs, %3 Keys")
                        .arg(pool->getGroupCount())
                        .arg(totalUrls)
                        .arg(totalKeys));
    return true;
}

bool ConfigManager::save(const BackendPool *pool)
{
    QJsonObject root;
    root["listenPort"] = m_listenPort;
    root["retryCount"] = m_retryCount;
    root["cooldownSeconds"] = m_cooldownSeconds;
    root["timeoutSeconds"] = m_timeoutSeconds;
    root["correctionEnabled"] = m_correctionEnabled;
    root["localTokenCount"] = m_localTokenCount;
    root["currentGroupIndex"] = pool->getCurrentGroupIndex();

    // Save groups
    QJsonArray groupsArray;
    for (int i = 0; i < pool->getGroupCount(); i++) {
        BackendGroup group = pool->getGroup(i);
        QJsonObject groupObj;
        groupObj["name"] = group.name;

        // Save URLs
        QJsonArray urlsArray;
        for (const ApiUrl &url : group.urls) {
            QJsonObject obj;
            obj["url"] = url.url;
            obj["enabled"] = url.enabled;
            urlsArray.append(obj);
        }
        groupObj["urls"] = urlsArray;

        // Save Keys
        QJsonArray keysArray;
        for (const ApiKey &key : group.keys) {
            QJsonObject obj;
            obj["key"] = key.key;
            obj["label"] = key.label;
            obj["enabled"] = key.enabled;
            keysArray.append(obj);
        }
        groupObj["keys"] = keysArray;

        // Save Model Mappings
        QJsonArray mappingsArray;
        for (const ModelMapping &mapping : group.modelMappings) {
            QJsonObject obj;
            obj["sourceModel"] = mapping.sourceModel;
            obj["targetModel"] = mapping.targetModel;
            mappingsArray.append(obj);
        }
        groupObj["modelMappings"] = mappingsArray;

        // Save OpenAI format setting
        groupObj["useOpenAIFormat"] = group.useOpenAIFormat;

        groupsArray.append(groupObj);
    }
    root["groups"] = groupsArray;

    QJsonDocument doc(root);

    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit logMessage(QString("Failed to save config: %1").arg(file.errorString()));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    emit logMessage(QString("Config saved to: %1").arg(m_configPath));
    return true;
}

int ConfigManager::listenPort() const
{
    return m_listenPort;
}

void ConfigManager::setListenPort(int port)
{
    m_listenPort = port;
}

int ConfigManager::retryCount() const
{
    return m_retryCount;
}

void ConfigManager::setRetryCount(int count)
{
    m_retryCount = count;
}

int ConfigManager::cooldownSeconds() const
{
    return m_cooldownSeconds;
}

void ConfigManager::setCooldownSeconds(int seconds)
{
    m_cooldownSeconds = seconds;
}

int ConfigManager::timeoutSeconds() const
{
    return m_timeoutSeconds;
}

void ConfigManager::setTimeoutSeconds(int seconds)
{
    // Clamp to valid range: 30-600 seconds
    m_timeoutSeconds = qBound(30, seconds, 600);
}

bool ConfigManager::correctionEnabled() const
{
    return m_correctionEnabled;
}

void ConfigManager::setCorrectionEnabled(bool enabled)
{
    m_correctionEnabled = enabled;
}

bool ConfigManager::localTokenCount() const
{
    return m_localTokenCount;
}

void ConfigManager::setLocalTokenCount(bool enabled)
{
    m_localTokenCount = enabled;
}
