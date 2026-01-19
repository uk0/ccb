#ifndef BACKENDPOOL_H
#define BACKENDPOOL_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QDateTime>
#include <QMutex>

struct ApiUrl {
    QString url;
    bool enabled = true;
    bool available = true;
    QDateTime cooldownUntil;
};

struct ApiKey {
    QString key;
    QString label;
    bool enabled = true;
    bool available = true;
    QDateTime cooldownUntil;
};

struct ModelMapping {
    QString sourceModel;  // Model name from Claude Code
    QString targetModel;  // Model name for backend
};

/**
 * BackendSnapshot - Atomic snapshot of backend state for thread-safe access.
 * All values are captured under a single lock to prevent race conditions.
 */
struct BackendSnapshot {
    QString url;
    QString key;
    int urlIndex = -1;
    int keyIndex = -1;
    bool openAIFormat = false;
    bool valid = false;  // True if backend is available
};

struct BackendGroup {
    QString name;
    QVector<ApiUrl> urls;
    QVector<ApiKey> keys;
    QVector<ModelMapping> modelMappings;
    bool useOpenAIFormat = false;  // Convert Claude API to OpenAI API format
    int currentUrlIndex = 0;
    int currentKeyIndex = 0;
};

class BackendPool : public QObject
{
    Q_OBJECT

public:
    explicit BackendPool(QObject *parent = nullptr);

    // Group management
    void addGroup(const QString &name);
    void removeGroup(int index);
    void renameGroup(int index, const QString &name);
    int getGroupCount() const;
    QStringList getGroupNames() const;
    int getCurrentGroupIndex() const;
    QString getCurrentGroupName() const;
    void setCurrentGroup(int index);
    BackendGroup getGroup(int index) const;
    QVector<BackendGroup>& groups();

    // URL management (operates on current group)
    void addUrl(const QString &url, bool enabled = true);
    void removeUrl(int index);
    void setUrlEnabled(int index, bool enabled);
    void markUrlUnavailable(int index, int cooldownSeconds = 60);
    void markUrlAvailable(int index);
    QVector<ApiUrl> getUrls() const;  // Thread-safe copy
    QVector<ApiUrl> &urls();  // For internal use only - not thread safe

    // Key management (operates on current group)
    void addKey(const QString &key, const QString &label = QString(), bool enabled = true);
    void removeKey(int index);
    void setKeyEnabled(int index, bool enabled);
    void markKeyUnavailable(int index, int cooldownSeconds = 0);
    void markKeyAvailable(int index);
    QVector<ApiKey> getKeys() const;  // Thread-safe copy
    QVector<ApiKey> &keys();  // For internal use only - not thread safe

    // Model mapping management (operates on current group)
    void addModelMapping(const QString &sourceModel, const QString &targetModel);
    void removeModelMapping(int index);
    void updateModelMapping(int index, const QString &sourceModel, const QString &targetModel);
    QVector<ModelMapping> getModelMappings() const;
    QString mapModelName(const QString &modelName) const;      // source -> target
    QString reverseMapModelName(const QString &modelName) const; // target -> source

    // OpenAI format setting (operates on current group)
    bool isOpenAIFormatEnabled() const;
    void setOpenAIFormatEnabled(bool enabled);

    // Selection
    int getCurrentUrlIndex() const;
    int getCurrentKeyIndex() const;
    QString getCurrentUrl() const;
    QString getCurrentKey() const;
    void setCurrentUrlIndex(int index);
    void setCurrentKeyIndex(int index);

    // Failover
    bool switchToNextUrl();
    bool switchToNextKey();
    void resetUrlIndex();
    void resetKeyIndex();

    // Refresh cooldowns
    void refreshCooldowns();

    // Status
    int availableUrlCount() const;
    int availableKeyCount() const;
    bool hasAvailableBackend() const;

    // Thread-safe atomic snapshot
    BackendSnapshot getBackendSnapshot() const;

signals:
    void urlSwitched(int newIndex, const QString &url);
    void keySwitched(int newIndex, const QString &label);
    void groupSwitched(int newIndex, const QString &name);
    void poolExhausted(const QString &type);
    void logMessage(const QString &message);

private:
    QVector<BackendGroup> m_groups;
    int m_currentGroupIndex = 0;
    mutable QMutex m_mutex;

    BackendGroup& currentGroup();
    const BackendGroup& currentGroup() const;
    int findNextAvailable(const QVector<ApiUrl> &list, int currentIndex) const;
    int findNextAvailableKey(const QVector<ApiKey> &list, int currentIndex) const;
};

#endif // BACKENDPOOL_H
