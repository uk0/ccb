#include "backendpool.h"
#include <QMutexLocker>

BackendPool::BackendPool(QObject *parent)
    : QObject(parent)
{
    // Create default group
    BackendGroup defaultGroup;
    defaultGroup.name = "Anthropic Official";
    m_groups.append(defaultGroup);
}

// Group management
void BackendPool::addGroup(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup group;
    group.name = name;
    m_groups.append(group);
}

void BackendPool::removeGroup(int index)
{
    QMutexLocker locker(&m_mutex);
    if (index >= 0 && index < m_groups.size() && m_groups.size() > 1) {
        m_groups.removeAt(index);
        if (m_currentGroupIndex >= m_groups.size()) {
            m_currentGroupIndex = m_groups.size() - 1;
        }
        if (m_currentGroupIndex == index) {
            m_currentGroupIndex = 0;
        }
    }
}

void BackendPool::renameGroup(int index, const QString &name)
{
    QMutexLocker locker(&m_mutex);
    if (index >= 0 && index < m_groups.size()) {
        m_groups[index].name = name;
    }
}

int BackendPool::getGroupCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_groups.size();
}

QStringList BackendPool::getGroupNames() const
{
    QMutexLocker locker(&m_mutex);
    QStringList names;
    for (const BackendGroup &group : m_groups) {
        names.append(group.name);
    }
    return names;
}

int BackendPool::getCurrentGroupIndex() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentGroupIndex;
}

QString BackendPool::getCurrentGroupName() const
{
    QMutexLocker locker(&m_mutex);
    if (m_currentGroupIndex >= 0 && m_currentGroupIndex < m_groups.size()) {
        return m_groups[m_currentGroupIndex].name;
    }
    return QString();
}

void BackendPool::setCurrentGroup(int index)
{
    QString groupName;
    bool shouldEmit = false;

    {
        QMutexLocker locker(&m_mutex);
        if (index >= 0 && index < m_groups.size() && index != m_currentGroupIndex) {
            m_currentGroupIndex = index;
            groupName = m_groups[index].name;
            shouldEmit = true;
        }
    }
    // Emit signals after releasing the lock to prevent deadlock
    if (shouldEmit) {
        emit logMessage(QString("Switched to group: %1").arg(groupName));
        emit groupSwitched(index, groupName);
    }
}

BackendGroup BackendPool::getGroup(int index) const
{
    QMutexLocker locker(&m_mutex);
    if (index >= 0 && index < m_groups.size()) {
        return m_groups[index];
    }
    return BackendGroup();
}

QVector<BackendGroup>& BackendPool::groups()
{
    return m_groups;
}

BackendGroup& BackendPool::currentGroup()
{
    if (m_groups.isEmpty()) {
        BackendGroup defaultGroup;
        defaultGroup.name = "Anthropic Official";
        m_groups.append(defaultGroup);
    }
    if (m_currentGroupIndex < 0 || m_currentGroupIndex >= m_groups.size()) {
        m_currentGroupIndex = 0;
    }
    return m_groups[m_currentGroupIndex];
}

const BackendGroup& BackendPool::currentGroup() const
{
    static BackendGroup emptyGroup;
    if (m_groups.isEmpty()) {
        return emptyGroup;
    }
    if (m_currentGroupIndex < 0 || m_currentGroupIndex >= m_groups.size()) {
        return m_groups[0];
    }
    return m_groups[m_currentGroupIndex];
}

void BackendPool::addUrl(const QString &url, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    ApiUrl apiUrl;
    apiUrl.url = url;
    apiUrl.enabled = enabled;
    apiUrl.available = true;
    currentGroup().urls.append(apiUrl);
}

void BackendPool::removeUrl(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.urls.size()) {
        group.urls.removeAt(index);
        if (group.currentUrlIndex >= group.urls.size()) {
            group.currentUrlIndex = 0;
        }
    }
}

void BackendPool::setUrlEnabled(int index, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.urls.size()) {
        group.urls[index].enabled = enabled;
    }
}

void BackendPool::markUrlUnavailable(int index, int cooldownSeconds)
{
    QString urlStr;
    {
        QMutexLocker locker(&m_mutex);
        BackendGroup &group = currentGroup();
        if (index >= 0 && index < group.urls.size()) {
            group.urls[index].available = false;
            if (cooldownSeconds > 0) {
                group.urls[index].cooldownUntil = QDateTime::currentDateTime().addSecs(cooldownSeconds);
            }
            urlStr = group.urls[index].url;
        }
    }
    // Emit signal outside lock to prevent deadlock
    if (!urlStr.isEmpty()) {
        emit logMessage(QString("URL %1 marked unavailable, cooldown: %2s")
                            .arg(urlStr)
                            .arg(cooldownSeconds));
    }
}

void BackendPool::markUrlAvailable(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.urls.size()) {
        group.urls[index].available = true;
        group.urls[index].cooldownUntil = QDateTime();
    }
}

QVector<ApiUrl> &BackendPool::urls()
{
    return currentGroup().urls;
}

QVector<ApiUrl> BackendPool::getUrls() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().urls;  // Returns a thread-safe copy
}

void BackendPool::addKey(const QString &key, const QString &label, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    ApiKey apiKey;
    apiKey.key = key;
    apiKey.label = label.isEmpty() ? QString("Key %1").arg(group.keys.size() + 1) : label;
    apiKey.enabled = enabled;
    apiKey.available = true;
    group.keys.append(apiKey);
}

void BackendPool::removeKey(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.keys.size()) {
        group.keys.removeAt(index);
        if (group.currentKeyIndex >= group.keys.size()) {
            group.currentKeyIndex = 0;
        }
    }
}

void BackendPool::setKeyEnabled(int index, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.keys.size()) {
        group.keys[index].enabled = enabled;
    }
}

void BackendPool::markKeyUnavailable(int index, int cooldownSeconds)
{
    QString keyLabel;
    {
        QMutexLocker locker(&m_mutex);
        BackendGroup &group = currentGroup();
        if (index >= 0 && index < group.keys.size()) {
            group.keys[index].available = false;
            if (cooldownSeconds > 0) {
                group.keys[index].cooldownUntil = QDateTime::currentDateTime().addSecs(cooldownSeconds);
            }
            keyLabel = group.keys[index].label;
        }
    }
    // Emit signal outside lock to prevent deadlock
    if (!keyLabel.isEmpty()) {
        emit logMessage(QString("Key '%1' marked unavailable, cooldown: %2s")
                            .arg(keyLabel)
                            .arg(cooldownSeconds));
    }
}

void BackendPool::markKeyAvailable(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.keys.size()) {
        group.keys[index].available = true;
        group.keys[index].cooldownUntil = QDateTime();
    }
}

QVector<ApiKey> &BackendPool::keys()
{
    return currentGroup().keys;
}

QVector<ApiKey> BackendPool::getKeys() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().keys;  // Returns a thread-safe copy
}

// Model mapping management
void BackendPool::addModelMapping(const QString &sourceModel, const QString &targetModel)
{
    QMutexLocker locker(&m_mutex);
    ModelMapping mapping;
    mapping.sourceModel = sourceModel;
    mapping.targetModel = targetModel;
    currentGroup().modelMappings.append(mapping);
}

void BackendPool::removeModelMapping(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.modelMappings.size()) {
        group.modelMappings.removeAt(index);
    }
}

void BackendPool::updateModelMapping(int index, const QString &sourceModel, const QString &targetModel)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.modelMappings.size()) {
        group.modelMappings[index].sourceModel = sourceModel;
        group.modelMappings[index].targetModel = targetModel;
    }
}

QVector<ModelMapping> BackendPool::getModelMappings() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().modelMappings;
}

QString BackendPool::mapModelName(const QString &modelName) const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    for (const ModelMapping &mapping : group.modelMappings) {
        if (mapping.sourceModel == modelName) {
            return mapping.targetModel;
        }
    }
    return modelName;  // No mapping found, return original
}

QString BackendPool::reverseMapModelName(const QString &modelName) const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    for (const ModelMapping &mapping : group.modelMappings) {
        if (mapping.targetModel == modelName) {
            return mapping.sourceModel;
        }
    }
    return modelName;  // No mapping found, return original
}

bool BackendPool::isOpenAIFormatEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().useOpenAIFormat;
}

void BackendPool::setOpenAIFormatEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    currentGroup().useOpenAIFormat = enabled;
}

int BackendPool::getCurrentUrlIndex() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().currentUrlIndex;
}

int BackendPool::getCurrentKeyIndex() const
{
    QMutexLocker locker(&m_mutex);
    return currentGroup().currentKeyIndex;
}

QString BackendPool::getCurrentUrl() const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    if (group.urls.isEmpty()) return QString();
    if (group.currentUrlIndex >= 0 && group.currentUrlIndex < group.urls.size()) {
        return group.urls[group.currentUrlIndex].url;
    }
    return QString();
}

QString BackendPool::getCurrentKey() const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    if (group.keys.isEmpty()) return QString();
    if (group.currentKeyIndex >= 0 && group.currentKeyIndex < group.keys.size()) {
        return group.keys[group.currentKeyIndex].key;
    }
    return QString();
}

void BackendPool::setCurrentUrlIndex(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.urls.size()) {
        group.currentUrlIndex = index;
        QString url = group.urls[index].url;
        locker.unlock();
        emit urlSwitched(index, url);
        emit logMessage(QString("Manually switched to URL #%1").arg(index + 1));
    }
}

void BackendPool::setCurrentKeyIndex(int index)
{
    QMutexLocker locker(&m_mutex);
    BackendGroup &group = currentGroup();
    if (index >= 0 && index < group.keys.size()) {
        group.currentKeyIndex = index;
        QString label = group.keys[index].label.isEmpty()
                            ? QString("Key #%1").arg(index + 1)
                            : group.keys[index].label;
        locker.unlock();
        emit keySwitched(index, label);
        emit logMessage(QString("Manually switched to %1").arg(label));
    }
}

int BackendPool::findNextAvailable(const QVector<ApiUrl> &list, int currentIndex) const
{
    if (list.isEmpty()) return -1;

    int size = list.size();
    for (int i = 1; i <= size; ++i) {
        int idx = (currentIndex + i) % size;
        const ApiUrl &url = list[idx];
        if (url.enabled && url.available) {
            return idx;
        }
        if (url.enabled && !url.cooldownUntil.isNull() &&
            QDateTime::currentDateTime() >= url.cooldownUntil) {
            return idx;
        }
    }
    return -1;
}

int BackendPool::findNextAvailableKey(const QVector<ApiKey> &list, int currentIndex) const
{
    if (list.isEmpty()) return -1;

    int size = list.size();
    for (int i = 1; i <= size; ++i) {
        int idx = (currentIndex + i) % size;
        const ApiKey &key = list[idx];
        if (key.enabled && key.available) {
            return idx;
        }
        if (key.enabled && !key.cooldownUntil.isNull() &&
            QDateTime::currentDateTime() >= key.cooldownUntil) {
            return idx;
        }
    }
    return -1;
}

bool BackendPool::switchToNextUrl()
{
    int nextIndex = -1;
    QString urlStr;
    bool exhausted = false;

    {
        QMutexLocker locker(&m_mutex);
        BackendGroup &group = currentGroup();
        nextIndex = findNextAvailable(group.urls, group.currentUrlIndex);
        if (nextIndex >= 0 && nextIndex != group.currentUrlIndex) {
            group.currentUrlIndex = nextIndex;
            urlStr = group.urls[nextIndex].url;
        } else {
            exhausted = true;
        }
    }

    // Emit signals outside lock to prevent deadlock
    if (!urlStr.isEmpty()) {
        emit logMessage(QString("Switched to URL: %1").arg(urlStr));
        emit urlSwitched(nextIndex, urlStr);
        return true;
    }
    if (exhausted) {
        emit poolExhausted("URL");
    }
    return false;
}

bool BackendPool::switchToNextKey()
{
    int nextIndex = -1;
    QString keyLabel;
    bool exhausted = false;

    {
        QMutexLocker locker(&m_mutex);
        BackendGroup &group = currentGroup();
        nextIndex = findNextAvailableKey(group.keys, group.currentKeyIndex);
        if (nextIndex >= 0 && nextIndex != group.currentKeyIndex) {
            group.currentKeyIndex = nextIndex;
            keyLabel = group.keys[nextIndex].label;
        } else {
            exhausted = true;
        }
    }

    // Emit signals outside lock to prevent deadlock
    if (!keyLabel.isEmpty()) {
        emit logMessage(QString("Switched to Key: %1").arg(keyLabel));
        emit keySwitched(nextIndex, keyLabel);
        return true;
    }
    if (exhausted) {
        emit poolExhausted("Key");
    }
    return false;
}

void BackendPool::resetUrlIndex()
{
    QMutexLocker locker(&m_mutex);
    currentGroup().currentUrlIndex = 0;
}

void BackendPool::resetKeyIndex()
{
    QMutexLocker locker(&m_mutex);
    currentGroup().currentKeyIndex = 0;
}

void BackendPool::refreshCooldowns()
{
    QMutexLocker locker(&m_mutex);
    QDateTime now = QDateTime::currentDateTime();

    // Refresh cooldowns for all groups
    for (BackendGroup &group : m_groups) {
        for (ApiUrl &url : group.urls) {
            if (!url.available && !url.cooldownUntil.isNull() && now >= url.cooldownUntil) {
                url.available = true;
                url.cooldownUntil = QDateTime();
                emit logMessage(QString("URL %1 cooldown expired, now available").arg(url.url));
            }
        }

        for (ApiKey &key : group.keys) {
            if (!key.available && !key.cooldownUntil.isNull() && now >= key.cooldownUntil) {
                key.available = true;
                key.cooldownUntil = QDateTime();
                emit logMessage(QString("Key '%1' cooldown expired, now available").arg(key.label));
            }
        }
    }
}

int BackendPool::availableUrlCount() const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    int count = 0;
    QDateTime now = QDateTime::currentDateTime();
    for (const ApiUrl &url : group.urls) {
        if (url.enabled && (url.available || (!url.cooldownUntil.isNull() && now >= url.cooldownUntil))) {
            ++count;
        }
    }
    return count;
}

int BackendPool::availableKeyCount() const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    int count = 0;
    QDateTime now = QDateTime::currentDateTime();
    for (const ApiKey &key : group.keys) {
        if (key.enabled && (key.available || (!key.cooldownUntil.isNull() && now >= key.cooldownUntil))) {
            ++count;
        }
    }
    return count;
}

bool BackendPool::hasAvailableBackend() const
{
    QMutexLocker locker(&m_mutex);
    const BackendGroup &group = currentGroup();
    QDateTime now = QDateTime::currentDateTime();

    bool hasUrl = false;
    for (const ApiUrl &url : group.urls) {
        if (url.enabled && (url.available || (!url.cooldownUntil.isNull() && now >= url.cooldownUntil))) {
            hasUrl = true;
            break;
        }
    }
    if (!hasUrl) return false;

    for (const ApiKey &key : group.keys) {
        if (key.enabled && (key.available || (!key.cooldownUntil.isNull() && now >= key.cooldownUntil))) {
            return true;
        }
    }
    return false;
}

BackendSnapshot BackendPool::getBackendSnapshot() const
{
    QMutexLocker locker(&m_mutex);
    BackendSnapshot snapshot;

    const BackendGroup &group = currentGroup();

    // Get current URL
    if (!group.urls.isEmpty() && group.currentUrlIndex >= 0 && group.currentUrlIndex < group.urls.size()) {
        const ApiUrl &url = group.urls[group.currentUrlIndex];
        if (url.enabled && url.available) {
            snapshot.url = url.url;
            snapshot.urlIndex = group.currentUrlIndex;
        }
    }

    // Get current Key
    if (!group.keys.isEmpty() && group.currentKeyIndex >= 0 && group.currentKeyIndex < group.keys.size()) {
        const ApiKey &key = group.keys[group.currentKeyIndex];
        if (key.enabled && key.available) {
            snapshot.key = key.key;
            snapshot.keyIndex = group.currentKeyIndex;
        }
    }

    // Get OpenAI format setting
    snapshot.openAIFormat = group.useOpenAIFormat;

    // Snapshot is valid if both URL and Key are available
    snapshot.valid = !snapshot.url.isEmpty() && !snapshot.key.isEmpty();

    return snapshot;
}
