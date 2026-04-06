#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QStringList>
#include <QElapsedTimer>
#include <QComboBox>

class BackendPool;
class ConversationBrowser;
class ConfigManager;
class ProxyServer;
class LicenseManager;
class ClaudeSettingsManager;

// Forward declare ProxyServer::Stats
#include "proxyserver.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(LicenseManager *licenseManager, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onStartStopClicked();
    void onAddUrlClicked();
    void onRemoveUrlClicked();
    void onAddKeyClicked();
    void onRemoveKeyClicked();
    void onGroupChanged(int index);
    void onAddGroupClicked();
    void onRemoveGroupClicked();
    void onEditGroupClicked();
    void onGroupSwitched(int index, const QString &name);
    void onLogMessage(const QString &message);
    void onServerStarted(quint16 port);
    void onServerStopped();
    void onUrlSwitched(int index, const QString &url);
    void onKeySwitched(int index, const QString &label);
    void refreshCooldowns();
    void updateStatus();
    void showAbout();
    void showConversations();
    void showClaudeSettings();
    void flushLogBuffer();
    void onStatsUpdated(const ProxyServer::Stats &stats);
    void onUrlItemClicked(QListWidgetItem *item);
    void onKeyItemClicked(QListWidgetItem *item);

private:
    void setupUi();
    void setupMenuBar();
    void loadConfig();
    void saveConfig();
    void refreshUrlList();
    void refreshKeyList();
    void refreshGroupList();
    void appendLog(const QString &message);
    void scheduleListRefresh();
    bool checkPermissions();
    void showPermissionHelp();
    void showStyledWarning(const QString &title, const QString &message);
    bool showStyledConfirm(const QString &title, const QString &message, const QString &detail = QString());
    void showEditGroupDialog();
    void showSupportedPlatformsDialog(QWidget *parent);
    void detectClaudeMode();

    Ui::MainWindow *ui;

    LicenseManager *m_licenseManager;
    BackendPool *m_pool;
    ConfigManager *m_config;
    ProxyServer *m_server;
    ClaudeSettingsManager *m_claudeSettings;
    QTimer *m_cooldownTimer;
    QTimer *m_logFlushTimer;
    QTimer *m_listRefreshTimer;
    QStringList m_logBuffer;
    bool m_needRefreshUrlList = false;
    bool m_needRefreshKeyList = false;
    static const int MAX_LOG_LINES = 500;
    static const int LOG_FLUSH_INTERVAL = 200;  // ms
    static const int LIST_REFRESH_INTERVAL = 500;  // ms

    // UI elements
    QSpinBox *m_portSpinBox;
    QSpinBox *m_retrySpinBox;
    QSpinBox *m_cooldownSpinBox;
    QSpinBox *m_timeoutSpinBox;
    QCheckBox *m_correctionCheckBox;
    QCheckBox *m_localTokenCountCheckBox;
    QCheckBox *m_debugLogCheckBox;
    QPushButton *m_startStopBtn;
    QListWidget *m_urlList;
    QPushButton *m_addUrlBtn;
    QPushButton *m_removeUrlBtn;
    QListWidget *m_keyList;
    QPushButton *m_addKeyBtn;
    QPushButton *m_removeKeyBtn;
    QTextEdit *m_logView;
    QLabel *m_statusLabel;
    QLabel *m_currentUrlLabel;
    QLabel *m_currentKeyLabel;
    QLabel *m_statsLabel;
    QComboBox *m_groupComboBox;
    QPushButton *m_addGroupBtn;
    QPushButton *m_removeGroupBtn;
    QPushButton *m_editGroupBtn;
    QLabel *m_claudeModeLabel;
};

#endif // MAINWINDOW_H
