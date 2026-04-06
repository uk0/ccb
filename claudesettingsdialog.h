#ifndef CLAUDESETTINGSDIALOG_H
#define CLAUDESETTINGSDIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QMap>

class ClaudeSettingsManager;

class ClaudeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClaudeSettingsDialog(ClaudeSettingsManager *manager, QWidget *parent = nullptr);

    // Set proxy info for auto-sync
    void setProxyInfo(int port, bool running);

private slots:
    void onSave();
    void onReload();
    void onAddAllowRule();
    void onRemoveAllowRule();
    void onAddCustomEnv();
    void onRemoveCustomEnv();

private:
    void setupUi();
    void loadFromManager();
    void saveToManager();

    ClaudeSettingsManager *m_manager;

    // Auto-sync
    QCheckBox *m_autoSyncCheckBox;
    int m_proxyPort = 8079;
    bool m_proxyRunning = false;

    // Env toggle checkboxes (bool-like env vars)
    QCheckBox *m_disableNonEssentialCalls;
    QCheckBox *m_disableNonEssentialTraffic;
    QCheckBox *m_disableExperimentalBetas;
    QCheckBox *m_disableAttributionHeader;
    QCheckBox *m_disableAutoUpdater;
    QCheckBox *m_disableAutoMemory;
    QCheckBox *m_disableInstallChecks;
    QCheckBox *m_enableMcpCli;
    QCheckBox *m_enableAgentTeams;
    QCheckBox *m_enableLspTools;

    // Env value fields
    QLineEdit *m_baseUrlEdit;
    QLineEdit *m_authTokenEdit;
    QLineEdit *m_maxOutputTokensEdit;
    QLineEdit *m_apiTimeoutEdit;
    QLineEdit *m_mcpToolTimeoutEdit;

    // Custom env vars
    QListWidget *m_customEnvList;

    // Settings fields
    QLineEdit *m_modelEdit;
    QCheckBox *m_fastModeCheckBox;
    QCheckBox *m_skipDangerousCheckBox;
    QComboBox *m_teammateModeCombo;
    QComboBox *m_teammateModelCombo;
    QComboBox *m_permissionModeCombo;

    // Permission allow list
    QListWidget *m_allowList;

    // Known toggle env keys (value "1" = on, absent = off)
    static const QStringList TOGGLE_ENV_KEYS;
    // Known value env keys
    static const QStringList VALUE_ENV_KEYS;
};

#endif // CLAUDESETTINGSDIALOG_H
