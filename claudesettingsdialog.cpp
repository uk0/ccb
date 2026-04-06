#include "claudesettingsdialog.h"
#include "claudesettingsmanager.h"
#include "macosstylemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <QFormLayout>
#include <QFrame>
#include <QToolButton>

const QStringList ClaudeSettingsDialog::TOGGLE_ENV_KEYS = {
    "DISABLE_NON_ESSENTIAL_MODEL_CALLS",
    "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC",
    "CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS",
    "CLAUDE_CODE_ATTRIBUTION_HEADER",
    "DISABLE_AUTOUPDATER",
    "CLAUDE_CODE_DISABLE_AUTO_MEMORY",
    "DISABLE_INSTALLATION_CHECKS",
    "ENABLE_EXRERIMENTAL_MCP_CLI",
    "CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS",
    "ENABLE_LSP_TOOLS"
};

const QStringList ClaudeSettingsDialog::VALUE_ENV_KEYS = {
    "ANTHROPIC_BASE_URL",
    "ANTHROPIC_AUTH_TOKEN",
    "CLAUDE_CODE_MAX_OUTPUT_TOKENS",
    "API_TIMEOUT_MS",
    "MCP_TOOL_TIMEOUT"
};

// ── helpers ──────────────────────────────────────────────────────────────

static QFrame *makeSeparator(QWidget *parent)
{
    QFrame *sep = new QFrame(parent);
    sep->setFrameShape(QFrame::HLine);
    sep->setFixedHeight(1);
    auto &s = MacOSStyleManager::instance();
    sep->setStyleSheet(QString("background-color: %1; border: none;")
        .arg(s.separatorColor().name()));
    return sep;
}

static QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto &s = MacOSStyleManager::instance();
    QLabel *lbl = new QLabel(text, parent);
    lbl->setStyleSheet(QString(
        "font-size: 12px; font-weight: 600; color: %1; padding: 0;"
    ).arg(s.textColor().name()));
    return lbl;
}

static QLabel *makeSectionDesc(const QString &text, QWidget *parent)
{
    auto &s = MacOSStyleManager::instance();
    QLabel *lbl = new QLabel(text, parent);
    lbl->setWordWrap(true);
    lbl->setStyleSheet(QString(
        "font-size: 10px; color: %1; padding: 0 0 2px 0;"
    ).arg(s.secondaryTextColor().name()));
    return lbl;
}

// ── ctor ─────────────────────────────────────────────────────────────────

ClaudeSettingsDialog::ClaudeSettingsDialog(ClaudeSettingsManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle("Claude Code Settings");
    setMinimumSize(640, 600);
    resize(680, 660);
    setModal(true);

    auto &style = MacOSStyleManager::instance();
    style.applyToDialog(this);

    setupUi();
    loadFromManager();
}

void ClaudeSettingsDialog::setProxyInfo(int port, bool running)
{
    m_proxyPort = port;
    m_proxyRunning = running;
    if (m_baseUrlEdit && m_autoSyncCheckBox && m_autoSyncCheckBox->isChecked()) {
        m_baseUrlEdit->setText(QString("http://127.0.0.1:%1").arg(port));
    }
}

// ── UI ───────────────────────────────────────────────────────────────────

void ClaudeSettingsDialog::setupUi()
{
    auto &style = MacOSStyleManager::instance();

    // Lambda helpers
    auto makeLineEdit = [&](const QString &placeholder, int minW = 0) -> QLineEdit* {
        QLineEdit *e = new QLineEdit(this);
        e->setStyleSheet(style.getLineEditStyle());
        e->setPlaceholderText(placeholder);
        e->setFixedHeight(30);
        if (minW) e->setMinimumWidth(minW);
        return e;
    };

    auto makeFormLabel = [&](const QString &text) -> QLabel* {
        QLabel *l = new QLabel(text, this);
        l->setStyleSheet(QString("font-size: 11px; color: %1; font-weight: 500;")
            .arg(style.secondaryTextColor().name()));
        l->setFixedWidth(140);
        return l;
    };

    auto makeSmallBtn = [&](const QString &text, bool accent = false) -> QPushButton* {
        QPushButton *b = new QPushButton(text, this);
        b->setStyleSheet(accent ? style.getAccentButtonStyle() : style.getSecondaryButtonStyle());
        b->setFixedSize(64, 26);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    QString groupBoxStyle = QString(
        "QGroupBox { "
        "  border: 1px solid %1; border-radius: 6px; "
        "  margin-top: 14px; padding: 14px 12px 10px 12px; "
        "  font-size: 11px; font-weight: 600; color: %2; "
        "}"
        "QGroupBox::title { "
        "  subcontrol-origin: margin; subcontrol-position: top left; "
        "  left: 12px; padding: 0 4px; "
        "}"
    ).arg(style.borderColor().name(), style.secondaryTextColor().name());

    // ── Main layout ──
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(20, 16, 20, 16);

    // ── Header bar ──
    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 0, 4);

    QLabel *titleLabel = new QLabel("Claude Code", this);
    titleLabel->setStyleSheet(QString("font-size: 15px; font-weight: 700; color: %1;")
        .arg(style.textColor().name()));

    QLabel *pathLabel = new QLabel(m_manager->settingsPath(), this);
    pathLabel->setStyleSheet(QString("font-size: 10px; color: %1;")
        .arg(style.secondaryTextColor().name()));

    // Mode badge
    bool apiMode = m_manager->isApiMode();
    QLabel *modeBadge = new QLabel(apiMode ? "API" : "Subscription", this);
    QString badgeBg = apiMode ? "rgba(33,150,243,0.15)" : "rgba(76,175,80,0.15)";
    QString badgeFg = apiMode ? style.accentColor().name() : style.successColor().name();
    modeBadge->setStyleSheet(QString(
        "font-size: 10px; font-weight: 600; padding: 2px 10px; border-radius: 8px; "
        "background: %1; color: %2;"
    ).arg(badgeBg, badgeFg));

    QVBoxLayout *titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(1);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(pathLabel);
    headerLayout->addLayout(titleBlock);
    headerLayout->addStretch();
    headerLayout->addWidget(modeBadge, 0, Qt::AlignTop);
    mainLayout->addLayout(headerLayout);

    mainLayout->addWidget(makeSeparator(this));

    // ── Tab widget ──
    QTabWidget *tabWidget = new QTabWidget(this);
    tabWidget->setDocumentMode(true);
    tabWidget->setStyleSheet(QString(
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab {"
        "  padding: 6px 20px; font-size: 12px; border: none;"
        "  border-bottom: 2px solid transparent; color: %1;"
        "}"
        "QTabBar::tab:selected {"
        "  color: %2; font-weight: 600; border-bottom: 2px solid %2;"
        "}"
        "QTabBar::tab:hover { color: %2; }"
    ).arg(style.secondaryTextColor().name(), style.accentColor().name()));

    // ══════════════════════════════════════════════════════════════════════
    //  Tab 1 : Environment
    // ══════════════════════════════════════════════════════════════════════
    QWidget *envTab = new QWidget(this);
    QVBoxLayout *envLayout = new QVBoxLayout(envTab);
    envLayout->setSpacing(14);
    envLayout->setContentsMargins(4, 8, 4, 4);

    // ── Connection section ──
    envLayout->addWidget(makeSectionTitle("Connection", this));
    envLayout->addWidget(makeSectionDesc(
        "Configure how Claude Code connects to the API backend.", this));

    // Base URL row
    QHBoxLayout *urlRow = new QHBoxLayout;
    urlRow->setSpacing(8);
    urlRow->addWidget(makeFormLabel("Base URL"));
    m_baseUrlEdit = makeLineEdit("http://127.0.0.1:8079");
    urlRow->addWidget(m_baseUrlEdit, 1);

    m_autoSyncCheckBox = new QCheckBox("Auto-sync", this);
    m_autoSyncCheckBox->setStyleSheet(style.getCheckBoxStyle());
    m_autoSyncCheckBox->setToolTip("Auto-fill proxy URL when saving");
    urlRow->addWidget(m_autoSyncCheckBox);
    envLayout->addLayout(urlRow);

    // Auth token row
    QHBoxLayout *tokenRow = new QHBoxLayout;
    tokenRow->setSpacing(8);
    tokenRow->addWidget(makeFormLabel("Auth Token"));
    m_authTokenEdit = makeLineEdit("sk-...");
    m_authTokenEdit->setEchoMode(QLineEdit::Password);
    tokenRow->addWidget(m_authTokenEdit, 1);

    QToolButton *showTokenBtn = new QToolButton(this);
    showTokenBtn->setText("Show");
    showTokenBtn->setFixedSize(44, 30);
    showTokenBtn->setCursor(Qt::PointingHandCursor);
    showTokenBtn->setStyleSheet(QString(
        "QToolButton { font-size: 10px; border: 1px solid %1; border-radius: 4px; "
        "background: %2; color: %3; }"
        "QToolButton:hover { background: %4; }"
    ).arg(style.borderColor().name(), style.secondaryBackgroundColor().name(),
          style.secondaryTextColor().name(), style.tertiaryBackgroundColor().name()));
    connect(showTokenBtn, &QToolButton::clicked, this, [this, showTokenBtn]() {
        bool hidden = m_authTokenEdit->echoMode() == QLineEdit::Password;
        m_authTokenEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        showTokenBtn->setText(hidden ? "Hide" : "Show");
    });
    tokenRow->addWidget(showTokenBtn);
    envLayout->addLayout(tokenRow);

    envLayout->addWidget(makeSeparator(this));

    // ── Performance section ──
    envLayout->addWidget(makeSectionTitle("Performance", this));
    envLayout->addWidget(makeSectionDesc(
        "Timeout and token limits for API requests.", this));

    QGridLayout *perfGrid = new QGridLayout;
    perfGrid->setSpacing(8);
    perfGrid->setColumnMinimumWidth(0, 140);

    m_maxOutputTokensEdit = makeLineEdit("640000");
    m_apiTimeoutEdit = makeLineEdit("1200000");
    m_mcpToolTimeoutEdit = makeLineEdit("4500000");

    perfGrid->addWidget(makeFormLabel("Max Output Tokens"), 0, 0);
    perfGrid->addWidget(m_maxOutputTokensEdit, 0, 1);
    perfGrid->addWidget(makeFormLabel("API Timeout (ms)"), 1, 0);
    perfGrid->addWidget(m_apiTimeoutEdit, 1, 1);
    perfGrid->addWidget(makeFormLabel("MCP Tool Timeout"), 2, 0);
    perfGrid->addWidget(m_mcpToolTimeoutEdit, 2, 1);
    envLayout->addLayout(perfGrid);

    envLayout->addWidget(makeSeparator(this));

    // ── Feature Toggles ──
    envLayout->addWidget(makeSectionTitle("Feature Toggles", this));
    envLayout->addWidget(makeSectionDesc(
        "Enable or disable Claude Code features via environment variables.", this));

    auto makeToggle = [&](const QString &label, const QString &envKey) -> QCheckBox* {
        QCheckBox *cb = new QCheckBox(label, this);
        cb->setStyleSheet(style.getCheckBoxStyle());
        cb->setToolTip(envKey);
        return cb;
    };

    m_disableNonEssentialCalls = makeToggle("Disable Non-Essential Calls", "DISABLE_NON_ESSENTIAL_MODEL_CALLS");
    m_disableNonEssentialTraffic = makeToggle("Disable Non-Essential Traffic", "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC");
    m_disableExperimentalBetas = makeToggle("Disable Experimental Betas", "CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS");
    m_disableAttributionHeader = makeToggle("Disable Attribution Header", "CLAUDE_CODE_ATTRIBUTION_HEADER");
    m_disableAutoUpdater = makeToggle("Disable Auto Updater", "DISABLE_AUTOUPDATER");
    m_disableAutoMemory = makeToggle("Disable Auto Memory", "CLAUDE_CODE_DISABLE_AUTO_MEMORY");
    m_disableInstallChecks = makeToggle("Disable Install Checks", "DISABLE_INSTALLATION_CHECKS");
    m_enableMcpCli = makeToggle("Enable MCP CLI", "ENABLE_EXRERIMENTAL_MCP_CLI");
    m_enableAgentTeams = makeToggle("Enable Agent Teams", "CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS");
    m_enableLspTools = makeToggle("Enable LSP Tools", "ENABLE_LSP_TOOLS");

    // Two-column grid for toggles
    QGridLayout *toggleGrid = new QGridLayout;
    toggleGrid->setSpacing(6);
    toggleGrid->setContentsMargins(4, 0, 4, 0);

    toggleGrid->addWidget(m_disableNonEssentialCalls,  0, 0);
    toggleGrid->addWidget(m_disableNonEssentialTraffic, 0, 1);
    toggleGrid->addWidget(m_disableExperimentalBetas,  1, 0);
    toggleGrid->addWidget(m_disableAttributionHeader,  1, 1);
    toggleGrid->addWidget(m_disableAutoUpdater,        2, 0);
    toggleGrid->addWidget(m_disableAutoMemory,         2, 1);
    toggleGrid->addWidget(m_disableInstallChecks,      3, 0);
    toggleGrid->addWidget(m_enableMcpCli,              3, 1);
    toggleGrid->addWidget(m_enableAgentTeams,          4, 0);
    toggleGrid->addWidget(m_enableLspTools,            4, 1);

    envLayout->addLayout(toggleGrid);

    envLayout->addWidget(makeSeparator(this));

    // ── Custom Env Vars ──
    envLayout->addWidget(makeSectionTitle("Custom Variables", this));
    envLayout->addWidget(makeSectionDesc(
        "Additional environment variables not listed above.", this));

    m_customEnvList = new QListWidget(this);
    m_customEnvList->setStyleSheet(style.getListWidgetStyle());
    m_customEnvList->setFixedHeight(80);
    envLayout->addWidget(m_customEnvList);

    QHBoxLayout *customBtnLayout = new QHBoxLayout;
    customBtnLayout->setSpacing(6);
    QPushButton *addEnvBtn = makeSmallBtn("Add");
    QPushButton *removeEnvBtn = makeSmallBtn("Remove");
    connect(addEnvBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onAddCustomEnv);
    connect(removeEnvBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onRemoveCustomEnv);
    customBtnLayout->addWidget(addEnvBtn);
    customBtnLayout->addWidget(removeEnvBtn);
    customBtnLayout->addStretch();
    envLayout->addLayout(customBtnLayout);

    envLayout->addStretch();

    // Scroll wrapper
    QScrollArea *envScroll = new QScrollArea(this);
    envScroll->setWidget(envTab);
    envScroll->setWidgetResizable(true);
    envScroll->setFrameShape(QFrame::NoFrame);
    envScroll->setStyleSheet(style.getScrollBarStyle());
    tabWidget->addTab(envScroll, "Environment");

    // ══════════════════════════════════════════════════════════════════════
    //  Tab 2 : Settings
    // ══════════════════════════════════════════════════════════════════════
    QWidget *settingsTab = new QWidget(this);
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsTab);
    settingsLayout->setSpacing(14);
    settingsLayout->setContentsMargins(4, 8, 4, 4);

    // ── Model ──
    settingsLayout->addWidget(makeSectionTitle("Model", this));
    settingsLayout->addWidget(makeSectionDesc(
        "Primary model and output speed settings.", this));

    QHBoxLayout *modelRow = new QHBoxLayout;
    modelRow->setSpacing(8);
    modelRow->addWidget(makeFormLabel("Model"));
    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setStyleSheet(style.getLineEditStyle());
    m_modelEdit->setPlaceholderText("opus[1m], sonnet, haiku");
    m_modelEdit->setFixedHeight(30);
    modelRow->addWidget(m_modelEdit, 1);
    settingsLayout->addLayout(modelRow);

    QHBoxLayout *flagsRow = new QHBoxLayout;
    flagsRow->setSpacing(24);
    flagsRow->setContentsMargins(144, 0, 0, 0);  // align with field
    m_fastModeCheckBox = new QCheckBox("Fast Mode", this);
    m_fastModeCheckBox->setStyleSheet(style.getCheckBoxStyle());
    m_fastModeCheckBox->setToolTip("Faster output (same model, reduced latency)");
    m_skipDangerousCheckBox = new QCheckBox("Skip Dangerous Prompt", this);
    m_skipDangerousCheckBox->setStyleSheet(style.getCheckBoxStyle());
    m_skipDangerousCheckBox->setToolTip("Skip confirmation for dangerous-mode permissions");
    flagsRow->addWidget(m_fastModeCheckBox);
    flagsRow->addWidget(m_skipDangerousCheckBox);
    flagsRow->addStretch();
    settingsLayout->addLayout(flagsRow);

    settingsLayout->addWidget(makeSeparator(this));

    // ── Agent ──
    settingsLayout->addWidget(makeSectionTitle("Agent Teams", this));
    settingsLayout->addWidget(makeSectionDesc(
        "Configure how sub-agents are spawned and which model they use.", this));

    QGridLayout *agentGrid = new QGridLayout;
    agentGrid->setSpacing(8);
    agentGrid->setColumnMinimumWidth(0, 140);

    m_teammateModeCombo = new QComboBox(this);
    m_teammateModeCombo->addItems({"", "in-process", "subprocess"});
    m_teammateModeCombo->setFixedHeight(30);
    m_teammateModeCombo->setToolTip("in-process: shared memory\nsubprocess: isolated process");

    m_teammateModelCombo = new QComboBox(this);
    m_teammateModelCombo->setEditable(true);
    m_teammateModelCombo->addItems({"", "opus", "sonnet", "haiku"});
    m_teammateModelCombo->setFixedHeight(30);
    m_teammateModelCombo->setToolTip("Default model for teammate agents");

    agentGrid->addWidget(makeFormLabel("Teammate Mode"), 0, 0);
    agentGrid->addWidget(m_teammateModeCombo, 0, 1);
    agentGrid->addWidget(makeFormLabel("Teammate Model"), 1, 0);
    agentGrid->addWidget(m_teammateModelCombo, 1, 1);
    settingsLayout->addLayout(agentGrid);

    settingsLayout->addWidget(makeSeparator(this));

    // ── Permissions ──
    settingsLayout->addWidget(makeSectionTitle("Permissions", this));
    settingsLayout->addWidget(makeSectionDesc(
        "Control how Claude Code requests tool permissions.", this));

    QHBoxLayout *modeRow = new QHBoxLayout;
    modeRow->setSpacing(8);
    modeRow->addWidget(makeFormLabel("Default Mode"));
    m_permissionModeCombo = new QComboBox(this);
    m_permissionModeCombo->addItems({"plan", "auto", "default", "bypassPermissions", "acceptEdits"});
    m_permissionModeCombo->setFixedHeight(30);
    m_permissionModeCombo->setMinimumWidth(180);
    modeRow->addWidget(m_permissionModeCombo);
    modeRow->addStretch();
    settingsLayout->addLayout(modeRow);

    QLabel *allowTitle = new QLabel("Allow List", this);
    allowTitle->setStyleSheet(QString("font-size: 11px; font-weight: 500; color: %1; margin-top: 4px;")
        .arg(style.secondaryTextColor().name()));
    settingsLayout->addWidget(allowTitle);

    m_allowList = new QListWidget(this);
    m_allowList->setStyleSheet(style.getListWidgetStyle());
    m_allowList->setFixedHeight(110);
    settingsLayout->addWidget(m_allowList);

    QHBoxLayout *allowBtnLayout = new QHBoxLayout;
    allowBtnLayout->setSpacing(6);
    QPushButton *addAllowBtn = makeSmallBtn("Add");
    QPushButton *removeAllowBtn = makeSmallBtn("Remove");
    connect(addAllowBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onAddAllowRule);
    connect(removeAllowBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onRemoveAllowRule);
    allowBtnLayout->addWidget(addAllowBtn);
    allowBtnLayout->addWidget(removeAllowBtn);
    allowBtnLayout->addStretch();
    settingsLayout->addLayout(allowBtnLayout);

    settingsLayout->addStretch();

    QScrollArea *settingsScroll = new QScrollArea(this);
    settingsScroll->setWidget(settingsTab);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setStyleSheet(style.getScrollBarStyle());
    tabWidget->addTab(settingsScroll, "Settings");

    mainLayout->addWidget(tabWidget, 1);

    // ── Bottom button bar ──
    mainLayout->addWidget(makeSeparator(this));

    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    buttonLayout->addStretch();

    QPushButton *reloadBtn = new QPushButton("Reload", this);
    reloadBtn->setStyleSheet(style.getSecondaryButtonStyle());
    reloadBtn->setFixedSize(80, 32);
    reloadBtn->setCursor(Qt::PointingHandCursor);
    reloadBtn->setToolTip("Reload settings from file");
    connect(reloadBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onReload);

    QPushButton *cancelBtn = new QPushButton("Cancel", this);
    cancelBtn->setStyleSheet(style.getSecondaryButtonStyle());
    cancelBtn->setFixedSize(80, 32);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet(style.getAccentButtonStyle());
    saveBtn->setFixedSize(80, 32);
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QPushButton::clicked, this, &ClaudeSettingsDialog::onSave);

    buttonLayout->addWidget(reloadBtn);
    buttonLayout->addSpacing(6);
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addSpacing(6);
    buttonLayout->addWidget(saveBtn);
    mainLayout->addLayout(buttonLayout);
}

// ── Data ↔ UI ────────────────────────────────────────────────────────────

void ClaudeSettingsDialog::loadFromManager()
{
    m_manager->load();

    auto setToggle = [&](QCheckBox *cb, const QString &key) {
        cb->setChecked(m_manager->hasEnvVar(key));
    };

    setToggle(m_disableNonEssentialCalls, "DISABLE_NON_ESSENTIAL_MODEL_CALLS");
    setToggle(m_disableNonEssentialTraffic, "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC");
    setToggle(m_disableExperimentalBetas, "CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS");
    setToggle(m_disableAttributionHeader, "CLAUDE_CODE_ATTRIBUTION_HEADER");
    setToggle(m_disableAutoUpdater, "DISABLE_AUTOUPDATER");
    setToggle(m_disableAutoMemory, "CLAUDE_CODE_DISABLE_AUTO_MEMORY");
    setToggle(m_disableInstallChecks, "DISABLE_INSTALLATION_CHECKS");
    setToggle(m_enableMcpCli, "ENABLE_EXRERIMENTAL_MCP_CLI");
    setToggle(m_enableAgentTeams, "CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS");
    setToggle(m_enableLspTools, "ENABLE_LSP_TOOLS");

    m_baseUrlEdit->setText(m_manager->envVar("ANTHROPIC_BASE_URL"));
    m_authTokenEdit->setText(m_manager->envVar("ANTHROPIC_AUTH_TOKEN"));
    m_maxOutputTokensEdit->setText(m_manager->envVar("CLAUDE_CODE_MAX_OUTPUT_TOKENS"));
    m_apiTimeoutEdit->setText(m_manager->envVar("API_TIMEOUT_MS"));
    m_mcpToolTimeoutEdit->setText(m_manager->envVar("MCP_TOOL_TIMEOUT"));

    // Custom env vars (not in toggle or value lists)
    m_customEnvList->clear();
    QMap<QString, QString> allEnv = m_manager->envVars();
    for (auto it = allEnv.begin(); it != allEnv.end(); ++it) {
        if (!TOGGLE_ENV_KEYS.contains(it.key()) && !VALUE_ENV_KEYS.contains(it.key())) {
            m_customEnvList->addItem(QString("%1=%2").arg(it.key(), it.value()));
        }
    }

    m_modelEdit->setText(m_manager->model());
    m_fastModeCheckBox->setChecked(m_manager->fastMode());
    m_skipDangerousCheckBox->setChecked(m_manager->skipDangerousMode());

    int tmIdx = m_teammateModeCombo->findText(m_manager->teammateMode());
    m_teammateModeCombo->setCurrentIndex(tmIdx >= 0 ? tmIdx : 0);

    QString tmModel = m_manager->teammateModel();
    int tmModelIdx = m_teammateModelCombo->findText(tmModel);
    if (tmModelIdx >= 0) {
        m_teammateModelCombo->setCurrentIndex(tmModelIdx);
    } else if (!tmModel.isEmpty()) {
        m_teammateModelCombo->setEditText(tmModel);
    }

    int pmIdx = m_permissionModeCombo->findText(m_manager->permissionDefaultMode());
    m_permissionModeCombo->setCurrentIndex(pmIdx >= 0 ? pmIdx : 0);

    m_allowList->clear();
    for (const QString &rule : m_manager->permissionAllowList()) {
        m_allowList->addItem(rule);
    }
}

void ClaudeSettingsDialog::saveToManager()
{
    auto saveToggle = [&](QCheckBox *cb, const QString &key, const QString &val = "1") {
        if (cb->isChecked()) {
            if (key == "CLAUDE_CODE_ATTRIBUTION_HEADER") {
                m_manager->setEnvVar(key, "0");
            } else {
                m_manager->setEnvVar(key, val);
            }
        } else {
            m_manager->removeEnvVar(key);
        }
    };

    saveToggle(m_disableNonEssentialCalls, "DISABLE_NON_ESSENTIAL_MODEL_CALLS");
    saveToggle(m_disableNonEssentialTraffic, "CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC");
    saveToggle(m_disableExperimentalBetas, "CLAUDE_CODE_DISABLE_EXPERIMENTAL_BETAS");
    saveToggle(m_disableAttributionHeader, "CLAUDE_CODE_ATTRIBUTION_HEADER");
    saveToggle(m_disableAutoUpdater, "DISABLE_AUTOUPDATER");
    saveToggle(m_disableAutoMemory, "CLAUDE_CODE_DISABLE_AUTO_MEMORY");
    saveToggle(m_disableInstallChecks, "DISABLE_INSTALLATION_CHECKS");
    saveToggle(m_enableMcpCli, "ENABLE_EXRERIMENTAL_MCP_CLI", "true");
    saveToggle(m_enableAgentTeams, "CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS");
    saveToggle(m_enableLspTools, "ENABLE_LSP_TOOLS", "true");

    auto saveEnvValue = [&](const QString &key, QLineEdit *edit) {
        QString val = edit->text().trimmed();
        if (!val.isEmpty()) {
            m_manager->setEnvVar(key, val);
        } else {
            m_manager->removeEnvVar(key);
        }
    };

    saveEnvValue("ANTHROPIC_BASE_URL", m_baseUrlEdit);
    saveEnvValue("ANTHROPIC_AUTH_TOKEN", m_authTokenEdit);
    saveEnvValue("CLAUDE_CODE_MAX_OUTPUT_TOKENS", m_maxOutputTokensEdit);
    saveEnvValue("API_TIMEOUT_MS", m_apiTimeoutEdit);
    saveEnvValue("MCP_TOOL_TIMEOUT", m_mcpToolTimeoutEdit);

    // Custom env vars: remove stale, re-add current
    QMap<QString, QString> allEnv = m_manager->envVars();
    for (auto it = allEnv.begin(); it != allEnv.end(); ++it) {
        if (!TOGGLE_ENV_KEYS.contains(it.key()) && !VALUE_ENV_KEYS.contains(it.key())) {
            m_manager->removeEnvVar(it.key());
        }
    }
    for (int i = 0; i < m_customEnvList->count(); i++) {
        QString text = m_customEnvList->item(i)->text();
        int eqPos = text.indexOf('=');
        if (eqPos > 0) {
            m_manager->setEnvVar(text.left(eqPos), text.mid(eqPos + 1));
        }
    }

    m_manager->setModel(m_modelEdit->text().trimmed());
    m_manager->setFastMode(m_fastModeCheckBox->isChecked());
    m_manager->setSkipDangerousMode(m_skipDangerousCheckBox->isChecked());
    m_manager->setTeammateMode(m_teammateModeCombo->currentText());
    m_manager->setTeammateModel(m_teammateModelCombo->currentText());

    m_manager->setPermissionDefaultMode(m_permissionModeCombo->currentText());
    QStringList allowList;
    for (int i = 0; i < m_allowList->count(); i++) {
        allowList.append(m_allowList->item(i)->text());
    }
    m_manager->setPermissionAllowList(allowList);
}

// ── Slots ────────────────────────────────────────────────────────────────

void ClaudeSettingsDialog::onSave()
{
    saveToManager();
    if (m_manager->save()) {
        accept();
    } else {
        QMessageBox::warning(this, "Error", "Failed to save Claude settings.");
    }
}

void ClaudeSettingsDialog::onReload()
{
    loadFromManager();
}

void ClaudeSettingsDialog::onAddAllowRule()
{
    QString rule = QInputDialog::getText(this, "Add Allow Rule",
        "Enter permission pattern (e.g., mcp__pencil, Bash(*)):");
    if (!rule.isEmpty()) {
        m_allowList->addItem(rule);
    }
}

void ClaudeSettingsDialog::onRemoveAllowRule()
{
    int row = m_allowList->currentRow();
    if (row >= 0) delete m_allowList->takeItem(row);
}

void ClaudeSettingsDialog::onAddCustomEnv()
{
    QString key = QInputDialog::getText(this, "Add Variable", "Variable name:");
    if (key.isEmpty()) return;
    QString value = QInputDialog::getText(this, "Add Variable",
        QString("Value for %1:").arg(key));
    m_customEnvList->addItem(QString("%1=%2").arg(key, value));
}

void ClaudeSettingsDialog::onRemoveCustomEnv()
{
    int row = m_customEnvList->currentRow();
    if (row >= 0) delete m_customEnvList->takeItem(row);
}
