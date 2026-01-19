#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "backendpool.h"
#include "configmanager.h"
#include "proxyserver.h"
#include "licensemanager.h"
#include "logger.h"
#include "macosstylemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QMenuBar>
#include <QDesktopServices>
#include <QUrl>
#include <QPixmap>
#include <QScrollBar>
#include <QScrollArea>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QDialog>
#include <QFrame>
#include <QLineEdit>
#include <QToolButton>
#include <QApplication>
#include <QStyle>

MainWindow::MainWindow(LicenseManager *licenseManager, QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_licenseManager(licenseManager)
    , m_pool(new BackendPool(this))
    , m_config(new ConfigManager(this))
    , m_server(nullptr)
    , m_cooldownTimer(new QTimer(this))
    , m_logFlushTimer(new QTimer(this))
    , m_listRefreshTimer(new QTimer(this))
{
    LOG("MainWindow: Constructor started");

    ui->setupUi(this);
    setWindowTitle("Claude Code Balance");
    resize(900, 650);

    // Initialize macOS style manager
    MacOSStyleManager::instance().initialize(this);

    // Connect theme change signal
    connect(&MacOSStyleManager::instance(), &MacOSStyleManager::themeChanged,
            this, [this](MacOSStyleManager::Theme) {
        // Refresh status colors when theme changes
        if (m_server && m_server->isRunning()) {
            onServerStarted(m_config->listenPort());
        } else {
            onServerStopped();
        }
    });

    // Check permissions first
    if (!checkPermissions()) {
        LOG("MainWindow: Permission check failed");
    }

    setupUi();
    setupMenuBar();
    loadConfig();

    m_server = new ProxyServer(m_pool, m_config, this);

    // Connect signals - all in main thread
    connect(m_pool, &BackendPool::logMessage, this, &MainWindow::onLogMessage);
    connect(m_pool, &BackendPool::urlSwitched, this, &MainWindow::onUrlSwitched);
    connect(m_pool, &BackendPool::keySwitched, this, &MainWindow::onKeySwitched);
    connect(m_pool, &BackendPool::groupSwitched, this, &MainWindow::onGroupSwitched);
    connect(m_config, &ConfigManager::logMessage, this, &MainWindow::onLogMessage);
    connect(m_server, &ProxyServer::logMessage, this, &MainWindow::onLogMessage);
    connect(m_server, &ProxyServer::started, this, &MainWindow::onServerStarted);
    connect(m_server, &ProxyServer::stopped, this, &MainWindow::onServerStopped);
    connect(m_server, &ProxyServer::statsUpdated, this, &MainWindow::onStatsUpdated);

    // Cooldown refresh timer - less frequent
    connect(m_cooldownTimer, &QTimer::timeout, this, &MainWindow::refreshCooldowns);
    m_cooldownTimer->start(10000);  // 10 seconds

    // Log buffer flush timer
    connect(m_logFlushTimer, &QTimer::timeout, this, &MainWindow::flushLogBuffer);
    m_logFlushTimer->start(LOG_FLUSH_INTERVAL);

    // List refresh timer - batched updates
    m_listRefreshTimer->setSingleShot(true);
    connect(m_listRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_needRefreshUrlList) {
            m_needRefreshUrlList = false;
            refreshUrlList();
        }
        if (m_needRefreshKeyList) {
            m_needRefreshKeyList = false;
            refreshKeyList();
        }
    });

    updateStatus();
    LOG("MainWindow: Constructor completed");
}

MainWindow::~MainWindow()
{
    LOG("MainWindow: Destructor");
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    LOG("MainWindow: closeEvent");
    saveConfig();
    if (m_server->isRunning()) {
        m_server->stop();
    }
    event->accept();
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 8, 12, 12);

    auto &style = MacOSStyleManager::instance();

    // Top control panel - two rows for better layout
    QWidget *controlPanel = new QWidget(this);
    QVBoxLayout *controlPanelLayout = new QVBoxLayout(controlPanel);
    controlPanelLayout->setSpacing(6);
    controlPanelLayout->setContentsMargins(0, 0, 0, 0);

    // Row 1: Settings and Start/Stop
    QWidget *row1Widget = new QWidget(this);
    QHBoxLayout *row1Layout = new QHBoxLayout(row1Widget);
    row1Layout->setSpacing(8);
    row1Layout->setContentsMargins(0, 0, 0, 0);

    // Port
    QLabel *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(style.secondaryTextColor().name()));
    m_portSpinBox = new QSpinBox(this);
    m_portSpinBox->setRange(1024, 65535);
    m_portSpinBox->setValue(8079);
    m_portSpinBox->setFixedWidth(70);
    m_portSpinBox->setToolTip("Proxy server listening port");

    // Retry
    QLabel *retryLabel = new QLabel("Retry:", this);
    retryLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(style.secondaryTextColor().name()));
    m_retrySpinBox = new QSpinBox(this);
    m_retrySpinBox->setRange(1, 15);
    m_retrySpinBox->setValue(3);
    m_retrySpinBox->setFixedWidth(50);
    m_retrySpinBox->setToolTip("Maximum retry attempts on failure");

    // Cooldown
    QLabel *cooldownLabel = new QLabel("CD:", this);
    cooldownLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(style.secondaryTextColor().name()));
    cooldownLabel->setToolTip("Cooldown time after backend failure");
    m_cooldownSpinBox = new QSpinBox(this);
    m_cooldownSpinBox->setRange(0, 30);
    m_cooldownSpinBox->setValue(3);
    m_cooldownSpinBox->setFixedWidth(50);
    m_cooldownSpinBox->setSuffix("s");
    m_cooldownSpinBox->setToolTip("Cooldown time in seconds after backend failure");

    // Timeout
    QLabel *timeoutLabel = new QLabel("Timeout:", this);
    timeoutLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(style.secondaryTextColor().name()));
    m_timeoutSpinBox = new QSpinBox(this);
    m_timeoutSpinBox->setRange(30, 600);
    m_timeoutSpinBox->setValue(300);
    m_timeoutSpinBox->setFixedWidth(65);
    m_timeoutSpinBox->setSuffix("s");
    m_timeoutSpinBox->setToolTip("Request timeout in seconds (30-600)");

    // Start/Stop button - accent color
    m_startStopBtn = new QPushButton("Start", this);
    m_startStopBtn->setFixedSize(80, 28);
    m_startStopBtn->setStyleSheet(style.getAccentButtonStyle());
    m_startStopBtn->setToolTip("Start or stop the proxy server");

    // Status label
    m_statusLabel = new QLabel("Stopped", this);
    m_statusLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
        .arg(style.secondaryTextColor().name()));
    m_statusLabel->setMinimumWidth(60);
    m_statusLabel->setToolTip("Current server status");

    row1Layout->addWidget(portLabel);
    row1Layout->addWidget(m_portSpinBox);
    row1Layout->addWidget(retryLabel);
    row1Layout->addWidget(m_retrySpinBox);
    row1Layout->addWidget(cooldownLabel);
    row1Layout->addWidget(m_cooldownSpinBox);
    row1Layout->addWidget(timeoutLabel);
    row1Layout->addWidget(m_timeoutSpinBox);
    row1Layout->addStretch();
    row1Layout->addWidget(m_startStopBtn);
    row1Layout->addWidget(m_statusLabel);

    // Row 2: Options checkboxes
    QWidget *row2Widget = new QWidget(this);
    QHBoxLayout *row2Layout = new QHBoxLayout(row2Widget);
    row2Layout->setSpacing(16);
    row2Layout->setContentsMargins(0, 0, 0, 0);

    // Options label
    QLabel *optionsLabel = new QLabel("Options:", this);
    optionsLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(style.secondaryTextColor().name()));

    // Correction checkbox with macOS style
    m_correctionCheckBox = new QCheckBox("Correction", this);
    m_correctionCheckBox->setChecked(true);
    m_correctionCheckBox->setToolTip("Auto-retry when server returns HTTP 200 with empty/null response.\nHelps recover from transient backend errors.");

    // Local token count checkbox
    m_localTokenCountCheckBox = new QCheckBox("Local Tokens", this);
    m_localTokenCountCheckBox->setChecked(true);
    m_localTokenCountCheckBox->setToolTip("Use fast local token estimation for count_tokens endpoint.\nNo API call needed, works only in OpenAI format mode.");

    row2Layout->addWidget(optionsLabel);
    row2Layout->addWidget(m_correctionCheckBox);
    row2Layout->addWidget(m_localTokenCountCheckBox);
    row2Layout->addStretch();

    controlPanelLayout->addWidget(row1Widget);
    controlPanelLayout->addWidget(row2Widget);

    mainLayout->addWidget(controlPanel);

    // Group selector row
    QWidget *groupWidget = new QWidget(this);
    QHBoxLayout *groupLayout = new QHBoxLayout(groupWidget);
    groupLayout->setSpacing(8);
    groupLayout->setContentsMargins(2, 0, 2, 0);

    QLabel *groupLabel = new QLabel("Group:", this);
    groupLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));

    m_groupComboBox = new QComboBox(this);
    m_groupComboBox->setMinimumWidth(180);
    m_groupComboBox->setFixedHeight(28);
    m_groupComboBox->setToolTip("Select backend group to use");

    m_addGroupBtn = new QPushButton("Add", this);
    m_addGroupBtn->setFixedSize(50, 28);
    m_addGroupBtn->setToolTip("Create a new backend group");

    m_removeGroupBtn = new QPushButton("Del", this);
    m_removeGroupBtn->setFixedSize(50, 28);
    m_removeGroupBtn->setToolTip("Delete the selected group");

    m_editGroupBtn = new QPushButton("Edit", this);
    m_editGroupBtn->setFixedSize(50, 28);
    m_editGroupBtn->setToolTip("Edit group settings, model mappings, and OpenAI format option");

    groupLayout->addWidget(groupLabel);
    groupLayout->addWidget(m_groupComboBox);
    groupLayout->addWidget(m_addGroupBtn);
    groupLayout->addWidget(m_removeGroupBtn);
    groupLayout->addWidget(m_editGroupBtn);
    groupLayout->addStretch();

    mainLayout->addWidget(groupWidget);

    // Current backend info - single line, minimal
    QWidget *infoWidget = new QWidget(this);
    QHBoxLayout *infoLayout = new QHBoxLayout(infoWidget);
    infoLayout->setSpacing(16);
    infoLayout->setContentsMargins(2, 0, 2, 0);

    m_currentUrlLabel = new QLabel("URL: -", this);
    m_currentUrlLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    m_currentUrlLabel->setToolTip("Currently active backend URL");

    m_currentKeyLabel = new QLabel("Key: -", this);
    m_currentKeyLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    m_currentKeyLabel->setToolTip("Currently active API key");

    // Stats label - show connections, requests, corrections
    m_statsLabel = new QLabel("", this);
    m_statsLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    m_statsLabel->setToolTip("Request statistics: Success / Errors / Corrections");

    infoLayout->addWidget(m_currentUrlLabel);
    infoLayout->addWidget(m_currentKeyLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_statsLabel);

    mainLayout->addWidget(infoWidget);

    // Splitter for config panels and log
    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(4);
    splitter->setChildrenCollapsible(false);

    // Config panel - horizontal split for URL and Keys
    QWidget *configPanel = new QWidget(this);
    QHBoxLayout *configLayout = new QHBoxLayout(configPanel);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setSpacing(8);

    // URL group - compact
    QGroupBox *urlGroup = new QGroupBox("URLS", this);
    QVBoxLayout *urlLayout = new QVBoxLayout(urlGroup);
    urlLayout->setSpacing(4);
    urlLayout->setContentsMargins(6, 10, 6, 6);

    m_urlList = new QListWidget(this);
    m_urlList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_urlList->setToolTip("Click to switch to URL, double-click to toggle enabled/disabled");

    QHBoxLayout *urlBtnLayout = new QHBoxLayout();
    urlBtnLayout->setSpacing(4);

    m_addUrlBtn = new QPushButton("Add", this);
    m_addUrlBtn->setFixedHeight(26);
    m_addUrlBtn->setToolTip("Add a new backend URL");

    m_removeUrlBtn = new QPushButton("Remove", this);
    m_removeUrlBtn->setFixedHeight(26);
    m_removeUrlBtn->setToolTip("Remove the selected URL");

    urlBtnLayout->addWidget(m_addUrlBtn);
    urlBtnLayout->addWidget(m_removeUrlBtn);
    urlBtnLayout->addStretch();

    urlLayout->addWidget(m_urlList);
    urlLayout->addLayout(urlBtnLayout);

    // Key group - compact
    QGroupBox *keyGroup = new QGroupBox("KEYS", this);
    QVBoxLayout *keyLayout = new QVBoxLayout(keyGroup);
    keyLayout->setSpacing(4);
    keyLayout->setContentsMargins(6, 10, 6, 6);

    m_keyList = new QListWidget(this);
    m_keyList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_keyList->setToolTip("Click to switch to Key, double-click to toggle enabled/disabled");

    QHBoxLayout *keyBtnLayout = new QHBoxLayout();
    keyBtnLayout->setSpacing(4);

    m_addKeyBtn = new QPushButton("Add", this);
    m_addKeyBtn->setFixedHeight(26);
    m_addKeyBtn->setToolTip("Add a new API key");

    m_removeKeyBtn = new QPushButton("Remove", this);
    m_removeKeyBtn->setFixedHeight(26);
    m_removeKeyBtn->setToolTip("Remove the selected API key");

    keyBtnLayout->addWidget(m_addKeyBtn);
    keyBtnLayout->addWidget(m_removeKeyBtn);
    keyBtnLayout->addStretch();

    keyLayout->addWidget(m_keyList);
    keyLayout->addLayout(keyBtnLayout);

    configLayout->addWidget(urlGroup);
    configLayout->addWidget(keyGroup);

    // Log group - terminal style with colored output
    QGroupBox *logGroup = new QGroupBox("LOGS", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logLayout->setSpacing(0);
    logLayout->setContentsMargins(6, 10, 6, 6);

    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setFont(QFont("SF Mono", 11));
    m_logView->setLineWrapMode(QTextEdit::NoWrap);
    m_logView->document()->setMaximumBlockCount(MAX_LOG_LINES);
    m_logView->setToolTip("Proxy server logs - scroll to see history");

    // Set log view style for better terminal look
    QString logBg = style.isDarkMode() ? "rgba(0, 0, 0, 0.4)" : "rgba(255, 255, 255, 0.9)";
    QString logBorder = style.isDarkMode() ? "rgba(255, 255, 255, 0.08)" : "rgba(0, 0, 0, 0.05)";
    m_logView->setStyleSheet(QString(
        "QTextEdit {"
        "    background-color: %1;"
        "    border: 1px solid %2;"
        "    border-radius: 6px;"
        "    padding: 6px;"
        "    font-family: 'SF Mono', Menlo, Monaco, monospace;"
        "    font-size: 11px;"
        "}"
    ).arg(logBg).arg(logBorder));

    logLayout->addWidget(m_logView);

    splitter->addWidget(configPanel);
    splitter->addWidget(logGroup);
    splitter->setSizes({180, 320});

    mainLayout->addWidget(splitter, 1);

    // Connect buttons
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStopClicked);
    connect(m_addUrlBtn, &QPushButton::clicked, this, &MainWindow::onAddUrlClicked);
    connect(m_removeUrlBtn, &QPushButton::clicked, this, &MainWindow::onRemoveUrlClicked);
    connect(m_addKeyBtn, &QPushButton::clicked, this, &MainWindow::onAddKeyClicked);
    connect(m_removeKeyBtn, &QPushButton::clicked, this, &MainWindow::onRemoveKeyClicked);

    // Connect list item clicks for manual switching
    connect(m_urlList, &QListWidget::itemClicked, this, &MainWindow::onUrlItemClicked);
    connect(m_keyList, &QListWidget::itemClicked, this, &MainWindow::onKeyItemClicked);

    connect(m_groupComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onGroupChanged);
    connect(m_addGroupBtn, &QPushButton::clicked, this, &MainWindow::onAddGroupClicked);
    connect(m_removeGroupBtn, &QPushButton::clicked, this, &MainWindow::onRemoveGroupClicked);
    connect(m_editGroupBtn, &QPushButton::clicked, this, &MainWindow::onEditGroupClicked);
}

void MainWindow::loadConfig()
{
    m_config->load(m_pool);
    m_portSpinBox->setValue(m_config->listenPort());
    m_retrySpinBox->setValue(m_config->retryCount());
    m_cooldownSpinBox->setValue(m_config->cooldownSeconds());
    m_timeoutSpinBox->setValue(m_config->timeoutSeconds());
    m_correctionCheckBox->setChecked(m_config->correctionEnabled());
    m_localTokenCountCheckBox->setChecked(m_config->localTokenCount());
    refreshGroupList();
    refreshUrlList();
    refreshKeyList();
    appendLog("Configuration loaded");
}

void MainWindow::saveConfig()
{
    m_config->setListenPort(m_portSpinBox->value());
    m_config->setRetryCount(m_retrySpinBox->value());
    m_config->setCooldownSeconds(m_cooldownSpinBox->value());
    m_config->setTimeoutSeconds(m_timeoutSpinBox->value());
    m_config->setCorrectionEnabled(m_correctionCheckBox->isChecked());
    m_config->setLocalTokenCount(m_localTokenCountCheckBox->isChecked());
    m_config->save(m_pool);
}

void MainWindow::refreshUrlList()
{
    // Save current selection
    int selectedRow = m_urlList->currentRow();

    m_urlList->clear();
    const auto urls = m_pool->getUrls();
    int currentIndex = m_pool->getCurrentUrlIndex();
    auto &style = MacOSStyleManager::instance();

    for (int i = 0; i < urls.size(); ++i) {
        QString text = urls[i].url;
        if (!urls[i].enabled) {
            text += " [Disabled]";
        } else if (!urls[i].available) {
            text += " [Unavailable]";
        }
        if (i == currentIndex) {
            text += " *";
        }

        QListWidgetItem *item = new QListWidgetItem(text, m_urlList);
        if (!urls[i].enabled) {
            item->setForeground(style.secondaryTextColor());
        } else if (!urls[i].available) {
            item->setForeground(style.dangerColor());
        } else if (i == currentIndex) {
            item->setForeground(style.successColor());
        }
    }

    // Restore selection if still valid
    if (selectedRow >= 0 && selectedRow < m_urlList->count()) {
        m_urlList->setCurrentRow(selectedRow);
    }
}

void MainWindow::refreshKeyList()
{
    // Save current selection
    int selectedRow = m_keyList->currentRow();

    m_keyList->clear();
    const auto keys = m_pool->getKeys();
    int currentIndex = m_pool->getCurrentKeyIndex();
    auto &style = MacOSStyleManager::instance();

    for (int i = 0; i < keys.size(); ++i) {
        QString text = keys[i].label;
        if (!keys[i].key.isEmpty()) {
            text += QString(" (%1...)").arg(keys[i].key.left(12));
        }
        if (!keys[i].enabled) {
            text += " [Disabled]";
        } else if (!keys[i].available) {
            text += " [Unavailable]";
        }
        if (i == currentIndex) {
            text += " *";
        }

        QListWidgetItem *item = new QListWidgetItem(text, m_keyList);
        if (!keys[i].enabled) {
            item->setForeground(style.secondaryTextColor());
        } else if (!keys[i].available) {
            item->setForeground(style.dangerColor());
        } else if (i == currentIndex) {
            item->setForeground(style.successColor());
        }
    }

    // Restore selection if still valid
    if (selectedRow >= 0 && selectedRow < m_keyList->count()) {
        m_keyList->setCurrentRow(selectedRow);
    }
}

void MainWindow::refreshGroupList()
{
    // Block signals to prevent triggering onGroupChanged during update
    m_groupComboBox->blockSignals(true);
    m_groupComboBox->clear();

    QStringList groupNames = m_pool->getGroupNames();
    for (const QString &name : groupNames) {
        m_groupComboBox->addItem(name);
    }

    m_groupComboBox->setCurrentIndex(m_pool->getCurrentGroupIndex());
    m_groupComboBox->blockSignals(false);

    // Update remove button state (can't remove if only one group)
    m_removeGroupBtn->setEnabled(m_pool->getGroupCount() > 1);
}

void MainWindow::scheduleListRefresh()
{
    // Batch list refresh requests
    if (!m_listRefreshTimer->isActive()) {
        m_listRefreshTimer->start(LIST_REFRESH_INTERVAL);
    }
}

void MainWindow::appendLog(const QString &message)
{
    // Also write to file
    LOG(message);

    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString lowerMsg = message.toLower();
    auto &style = MacOSStyleManager::instance();

    // Determine log type and color
    QString color;
    QString prefix;

    if (lowerMsg.contains("error") ||
        lowerMsg.contains("failed") ||
        lowerMsg.contains("unavailable") ||
        lowerMsg.contains("exhausted") ||
        lowerMsg.contains("exceeded") ||
        lowerMsg.contains("no available") ||
        lowerMsg.contains("max retries") ||
        lowerMsg.contains("rejected") ||
        lowerMsg.contains("timeout")) {
        // Error - Red
        color = style.dangerColor().name();
        prefix = "ERR";
    } else if (lowerMsg.contains("warn") ||
               lowerMsg.contains("retry") ||
               lowerMsg.contains("cooldown") ||
               lowerMsg.contains("switching") ||
               lowerMsg.contains("reset")) {
        // Warning - Orange
        color = style.warningColor().name();
        prefix = "WRN";
    } else if (lowerMsg.contains("correction") ||
               lowerMsg.contains("empty response") ||
               lowerMsg.contains("retrying")) {
        // Correction - Cyan/Blue
        color = style.accentColor().name();
        prefix = "COR";
    } else if (lowerMsg.contains("success") ||
               lowerMsg.contains("started") ||
               lowerMsg.contains("connected") ||
               lowerMsg.contains("completed") ||
               lowerMsg.contains("loaded")) {
        // Success - Green
        color = style.successColor().name();
        prefix = "OK ";
    } else {
        // Info - Secondary text color
        color = style.secondaryTextColor().name();
        prefix = "INF";
    }

    // Format with HTML
    QString formattedMsg = QString(
        "<span style='color: %1;'>[%2]</span> "
        "<span style='color: %3; font-weight: 500;'>[%4]</span> "
        "<span style='color: %5;'>%6</span>"
    ).arg(style.secondaryTextColor().name())  // timestamp color
     .arg(timestamp)
     .arg(color)                               // prefix color
     .arg(prefix)
     .arg(style.textColor().name())            // message color
     .arg(message.toHtmlEscaped());

    m_logBuffer.append(formattedMsg);

    // Limit buffer size
    while (m_logBuffer.size() > MAX_LOG_LINES) {
        m_logBuffer.removeFirst();
    }
}

void MainWindow::flushLogBuffer()
{
    if (m_logBuffer.isEmpty()) {
        return;
    }

    // Move buffer to local
    QStringList messagesToFlush;
    messagesToFlush.swap(m_logBuffer);

    // Check scroll position
    QScrollBar *scrollBar = m_logView->verticalScrollBar();
    bool wasAtBottom = scrollBar->value() >= scrollBar->maximum() - 10;

    // Append all messages as HTML
    for (const QString &msg : messagesToFlush) {
        m_logView->append(msg);
    }

    // Auto-scroll if was at bottom
    if (wasAtBottom) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

void MainWindow::onStartStopClicked()
{
    if (m_server->isRunning()) {
        m_server->stop();
    } else {
        if (m_pool->getUrls().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please add at least one API URL");
            return;
        }
        if (m_pool->getKeys().isEmpty()) {
            QMessageBox::warning(this, "Error", "Please add at least one API Key");
            return;
        }
        saveConfig();
        m_server->start(m_portSpinBox->value());
    }
}

void MainWindow::onAddUrlClicked()
{
    QString url = QInputDialog::getText(this, "Add API URL",
                                        "Enter API URL (e.g., https://api.anthropic.com):");
    if (!url.isEmpty()) {
        m_pool->addUrl(url);
        refreshUrlList();
        saveConfig();
        appendLog(QString("Added URL: %1").arg(url));
    }
}

void MainWindow::onRemoveUrlClicked()
{
    int row = m_urlList->currentRow();
    if (row >= 0) {
        auto urls = m_pool->getUrls();
        QString url = (row < urls.size()) ? urls[row].url : QString();
        m_pool->removeUrl(row);
        refreshUrlList();
        saveConfig();
        appendLog(QString("Removed URL: %1").arg(url));
    }
}

void MainWindow::onAddKeyClicked()
{
    QString key = QInputDialog::getText(this, "Add API Key",
                                        "Enter API Key:");
    if (!key.isEmpty()) {
        QString label = QInputDialog::getText(this, "Key Label",
                                              "Enter a label for this key (optional):");
        m_pool->addKey(key, label);
        refreshKeyList();
        saveConfig();
        appendLog(QString("Added Key: %1").arg(label.isEmpty() ? key.left(12) + "..." : label));
    }
}

void MainWindow::onRemoveKeyClicked()
{
    int row = m_keyList->currentRow();
    if (row >= 0) {
        auto keys = m_pool->getKeys();
        QString label = (row < keys.size()) ? keys[row].label : QString();
        m_pool->removeKey(row);
        refreshKeyList();
        saveConfig();
        appendLog(QString("Removed Key: %1").arg(label));
    }
}

void MainWindow::onGroupChanged(int index)
{
    if (index >= 0 && index < m_pool->getGroupCount()) {
        // Block signals in groupSwitched handler
        m_groupComboBox->blockSignals(true);
        m_pool->setCurrentGroup(index);
        m_groupComboBox->blockSignals(false);

        refreshUrlList();
        refreshKeyList();
        updateStatus();
        saveConfig();
    }
}

void MainWindow::onAddGroupClicked()
{
    QString name = QInputDialog::getText(this, "Add Group",
                                         "Enter group name:");
    if (!name.isEmpty()) {
        m_pool->addGroup(name);

        // Block signals to prevent loop
        m_groupComboBox->blockSignals(true);
        refreshGroupList();

        // Switch to the new group
        int newIndex = m_pool->getGroupCount() - 1;
        m_pool->setCurrentGroup(newIndex);
        m_groupComboBox->setCurrentIndex(newIndex);
        m_groupComboBox->blockSignals(false);

        refreshUrlList();
        refreshKeyList();
        updateStatus();
        saveConfig();
        appendLog(QString("Added group: %1").arg(name));
    }
}

void MainWindow::onRemoveGroupClicked()
{
    if (m_pool->getGroupCount() <= 1) {
        showStyledWarning("Error", "Cannot remove the last group.");
        return;
    }

    QString currentGroupName = m_pool->getCurrentGroupName();

    bool confirmed = showStyledConfirm(
        "Remove Group",
        QString("Are you sure you want to remove group '%1'?").arg(currentGroupName),
        "All URLs and Keys in this group will be deleted."
    );

    if (confirmed) {
        int currentIndex = m_pool->getCurrentGroupIndex();

        // Block signals to prevent loop
        m_groupComboBox->blockSignals(true);
        m_pool->removeGroup(currentIndex);
        refreshGroupList();
        m_groupComboBox->blockSignals(false);

        refreshUrlList();
        refreshKeyList();
        updateStatus();
        saveConfig();
        appendLog(QString("Removed group: %1").arg(currentGroupName));
    }
}

void MainWindow::onEditGroupClicked()
{
    showEditGroupDialog();
}

void MainWindow::showEditGroupDialog()
{
    auto &style = MacOSStyleManager::instance();

    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("Edit Group");
    dialog->setMinimumSize(520, 420);
    dialog->setModal(true);
    style.applyToDialog(dialog);

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // Group name
    QHBoxLayout *nameLayout = new QHBoxLayout;
    QLabel *nameLabel = new QLabel("Group Name:", dialog);
    nameLabel->setStyleSheet(QString("font-size: 13px; color: %1;")
        .arg(style.textColor().name()));
    QLineEdit *nameEdit = new QLineEdit(m_pool->getCurrentGroupName(), dialog);
    nameEdit->setStyleSheet(style.getLineEditStyle());
    nameEdit->setFixedHeight(32);
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit, 1);
    mainLayout->addLayout(nameLayout);

    // OpenAI Format checkbox with help button
    QHBoxLayout *openAILayout = new QHBoxLayout;
    openAILayout->setSpacing(8);

    QCheckBox *openAICheckBox = new QCheckBox("Use OpenAI API Format", dialog);
    openAICheckBox->setChecked(m_pool->isOpenAIFormatEnabled());
    openAICheckBox->setStyleSheet(style.getCheckBoxStyle());
    openAICheckBox->setToolTip("Convert Claude API requests to OpenAI Chat Completions format.\n"
                               "Enable this when using OpenAI-compatible backends.");

    QToolButton *helpBtn = new QToolButton(dialog);
    helpBtn->setText("?");
    helpBtn->setFixedSize(18, 18);
    helpBtn->setCursor(Qt::PointingHandCursor);
    helpBtn->setStyleSheet(QString(
        "QToolButton {"
        "  border: 1px solid %1;"
        "  border-radius: 9px;"
        "  background: %2;"
        "  color: %3;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "}"
        "QToolButton:hover {"
        "  background-color: %4;"
        "  border-color: %5;"
        "}"
    ).arg(style.borderColor().name(),
          style.secondaryBackgroundColor().name(),
          style.textColor().name(),
          style.tertiaryBackgroundColor().name(),
          style.accentColor().name()));
    helpBtn->setToolTip("View supported third-party platforms and models");

    connect(helpBtn, &QToolButton::clicked, this, [this, dialog]() {
        showSupportedPlatformsDialog(dialog);
    });

    openAILayout->addWidget(openAICheckBox);
    openAILayout->addSpacing(6);
    openAILayout->addWidget(helpBtn);
    openAILayout->addStretch();
    mainLayout->addLayout(openAILayout);

    // OpenAI format description
    QLabel *openAIDescLabel = new QLabel(
        "When enabled, the proxy converts Claude API format to OpenAI format,\n"
        "allowing you to use OpenAI-compatible backends (e.g., local LLMs, vLLM, Ollama).", dialog);
    openAIDescLabel->setWordWrap(true);
    openAIDescLabel->setStyleSheet(QString("font-size: 11px; color: %1; margin-left: 20px;")
        .arg(style.secondaryTextColor().name()));
    mainLayout->addWidget(openAIDescLabel);

    // Model Mappings section
    QGroupBox *mappingsGroup = new QGroupBox("Model Mappings", dialog);
    QVBoxLayout *mappingsLayout = new QVBoxLayout(mappingsGroup);
    mappingsLayout->setSpacing(8);

    // Mappings list
    QListWidget *mappingsList = new QListWidget(dialog);
    mappingsList->setStyleSheet(style.getListWidgetStyle());
    mappingsList->setMinimumHeight(150);

    // Load existing mappings
    auto mappings = m_pool->getModelMappings();
    for (const ModelMapping &mapping : mappings) {
        QString text = QString("%1 → %2").arg(mapping.sourceModel, mapping.targetModel);
        mappingsList->addItem(text);
    }

    mappingsLayout->addWidget(mappingsList);

    // Mapping buttons
    QHBoxLayout *mappingBtnLayout = new QHBoxLayout;
    mappingBtnLayout->setSpacing(8);

    QPushButton *addMappingBtn = new QPushButton("Add", dialog);
    addMappingBtn->setStyleSheet(style.getSecondaryButtonStyle());
    addMappingBtn->setFixedSize(70, 28);

    QPushButton *editMappingBtn = new QPushButton("Edit", dialog);
    editMappingBtn->setStyleSheet(style.getSecondaryButtonStyle());
    editMappingBtn->setFixedSize(70, 28);

    QPushButton *removeMappingBtn = new QPushButton("Remove", dialog);
    removeMappingBtn->setStyleSheet(style.getSecondaryButtonStyle());
    removeMappingBtn->setFixedSize(70, 28);

    mappingBtnLayout->addWidget(addMappingBtn);
    mappingBtnLayout->addWidget(editMappingBtn);
    mappingBtnLayout->addWidget(removeMappingBtn);
    mappingBtnLayout->addStretch();

    mappingsLayout->addLayout(mappingBtnLayout);
    mainLayout->addWidget(mappingsGroup);

    // Helper text
    QLabel *helpLabel = new QLabel(
        "Model mappings convert model names from Claude Code to backend format.\n"
        "Example: claude-sonnet-4-20250514 → claude-sonnet-4", dialog);
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
        .arg(style.secondaryTextColor().name()));
    mainLayout->addWidget(helpLabel);

    mainLayout->addStretch();

    // Dialog buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    QPushButton *cancelBtn = new QPushButton("Cancel", dialog);
    cancelBtn->setStyleSheet(style.getSecondaryButtonStyle());
    cancelBtn->setFixedSize(80, 32);
    connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);

    QPushButton *saveBtn = new QPushButton("Save", dialog);
    saveBtn->setStyleSheet(style.getAccentButtonStyle());
    saveBtn->setFixedSize(80, 32);

    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveBtn);
    mainLayout->addLayout(buttonLayout);

    // Store mappings data for editing
    QVector<ModelMapping> editedMappings = mappings;

    // Add mapping button
    connect(addMappingBtn, &QPushButton::clicked, dialog, [&, mappingsList, dialog]() {
        QDialog *addDialog = new QDialog(dialog);
        addDialog->setWindowTitle("Add Model Mapping");
        addDialog->setMinimumWidth(400);
        addDialog->setModal(true);
        style.applyToDialog(addDialog);

        QVBoxLayout *addLayout = new QVBoxLayout(addDialog);
        addLayout->setSpacing(12);
        addLayout->setContentsMargins(20, 20, 20, 20);

        QLabel *sourceLabel = new QLabel("Source Model (from Claude Code):", addDialog);
        sourceLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(style.textColor().name()));
        QLineEdit *sourceEdit = new QLineEdit(addDialog);
        sourceEdit->setStyleSheet(style.getLineEditStyle());
        sourceEdit->setPlaceholderText("e.g., claude-sonnet-4-20250514");

        QLabel *targetLabel = new QLabel("Target Model (for Backend):", addDialog);
        targetLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(style.textColor().name()));
        QLineEdit *targetEdit = new QLineEdit(addDialog);
        targetEdit->setStyleSheet(style.getLineEditStyle());
        targetEdit->setPlaceholderText("e.g., claude-sonnet-4");

        addLayout->addWidget(sourceLabel);
        addLayout->addWidget(sourceEdit);
        addLayout->addWidget(targetLabel);
        addLayout->addWidget(targetEdit);
        addLayout->addStretch();

        QHBoxLayout *addBtnLayout = new QHBoxLayout;
        addBtnLayout->addStretch();
        QPushButton *addCancelBtn = new QPushButton("Cancel", addDialog);
        addCancelBtn->setStyleSheet(style.getSecondaryButtonStyle());
        addCancelBtn->setFixedSize(70, 28);
        connect(addCancelBtn, &QPushButton::clicked, addDialog, &QDialog::reject);

        QPushButton *addOkBtn = new QPushButton("Add", addDialog);
        addOkBtn->setStyleSheet(style.getAccentButtonStyle());
        addOkBtn->setFixedSize(70, 28);
        connect(addOkBtn, &QPushButton::clicked, addDialog, &QDialog::accept);

        addBtnLayout->addWidget(addCancelBtn);
        addBtnLayout->addWidget(addOkBtn);
        addLayout->addLayout(addBtnLayout);

        if (addDialog->exec() == QDialog::Accepted) {
            QString source = sourceEdit->text().trimmed();
            QString target = targetEdit->text().trimmed();
            if (!source.isEmpty() && !target.isEmpty()) {
                ModelMapping newMapping;
                newMapping.sourceModel = source;
                newMapping.targetModel = target;
                editedMappings.append(newMapping);
                mappingsList->addItem(QString("%1 → %2").arg(source, target));
            }
        }
        addDialog->deleteLater();
    });

    // Edit mapping button
    connect(editMappingBtn, &QPushButton::clicked, dialog, [&, mappingsList, dialog]() {
        int row = mappingsList->currentRow();
        if (row < 0 || row >= editedMappings.size()) return;

        QDialog *editDialog = new QDialog(dialog);
        editDialog->setWindowTitle("Edit Model Mapping");
        editDialog->setMinimumWidth(400);
        editDialog->setModal(true);
        style.applyToDialog(editDialog);

        QVBoxLayout *editLayout = new QVBoxLayout(editDialog);
        editLayout->setSpacing(12);
        editLayout->setContentsMargins(20, 20, 20, 20);

        QLabel *sourceLabel = new QLabel("Source Model (from Claude Code):", editDialog);
        sourceLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(style.textColor().name()));
        QLineEdit *sourceEdit = new QLineEdit(editedMappings[row].sourceModel, editDialog);
        sourceEdit->setStyleSheet(style.getLineEditStyle());

        QLabel *targetLabel = new QLabel("Target Model (for Backend):", editDialog);
        targetLabel->setStyleSheet(QString("font-size: 12px; color: %1;").arg(style.textColor().name()));
        QLineEdit *targetEdit = new QLineEdit(editedMappings[row].targetModel, editDialog);
        targetEdit->setStyleSheet(style.getLineEditStyle());

        editLayout->addWidget(sourceLabel);
        editLayout->addWidget(sourceEdit);
        editLayout->addWidget(targetLabel);
        editLayout->addWidget(targetEdit);
        editLayout->addStretch();

        QHBoxLayout *editBtnLayout = new QHBoxLayout;
        editBtnLayout->addStretch();
        QPushButton *editCancelBtn = new QPushButton("Cancel", editDialog);
        editCancelBtn->setStyleSheet(style.getSecondaryButtonStyle());
        editCancelBtn->setFixedSize(70, 28);
        connect(editCancelBtn, &QPushButton::clicked, editDialog, &QDialog::reject);

        QPushButton *editOkBtn = new QPushButton("Save", editDialog);
        editOkBtn->setStyleSheet(style.getAccentButtonStyle());
        editOkBtn->setFixedSize(70, 28);
        connect(editOkBtn, &QPushButton::clicked, editDialog, &QDialog::accept);

        editBtnLayout->addWidget(editCancelBtn);
        editBtnLayout->addWidget(editOkBtn);
        editLayout->addLayout(editBtnLayout);

        if (editDialog->exec() == QDialog::Accepted) {
            QString source = sourceEdit->text().trimmed();
            QString target = targetEdit->text().trimmed();
            if (!source.isEmpty() && !target.isEmpty()) {
                editedMappings[row].sourceModel = source;
                editedMappings[row].targetModel = target;
                mappingsList->item(row)->setText(QString("%1 → %2").arg(source, target));
            }
        }
        editDialog->deleteLater();
    });

    // Remove mapping button
    connect(removeMappingBtn, &QPushButton::clicked, dialog, [&, mappingsList]() {
        int row = mappingsList->currentRow();
        if (row >= 0 && row < editedMappings.size()) {
            editedMappings.removeAt(row);
            delete mappingsList->takeItem(row);
        }
    });

    // Save button
    connect(saveBtn, &QPushButton::clicked, dialog, [&, nameEdit, openAICheckBox, dialog]() {
        QString newName = nameEdit->text().trimmed();
        if (newName.isEmpty()) {
            showStyledWarning("Error", "Group name cannot be empty.");
            return;
        }

        // Update group name
        m_pool->renameGroup(m_pool->getCurrentGroupIndex(), newName);

        // Update OpenAI format setting
        m_pool->setOpenAIFormatEnabled(openAICheckBox->isChecked());

        // Update model mappings - clear and re-add
        while (!m_pool->getModelMappings().isEmpty()) {
            m_pool->removeModelMapping(0);
        }
        for (const ModelMapping &mapping : editedMappings) {
            m_pool->addModelMapping(mapping.sourceModel, mapping.targetModel);
        }

        refreshGroupList();
        saveConfig();

        QString modeStr = openAICheckBox->isChecked() ? " [OpenAI Mode]" : "";
        appendLog(QString("Updated group: %1%2 with %3 model mappings")
            .arg(newName).arg(modeStr).arg(editedMappings.size()));

        dialog->accept();
    });

    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::onGroupSwitched(int index, const QString &name)
{
    Q_UNUSED(name)
    // Block signals to prevent loop
    m_groupComboBox->blockSignals(true);
    m_groupComboBox->setCurrentIndex(index);
    m_groupComboBox->blockSignals(false);

    refreshUrlList();
    refreshKeyList();
    updateStatus();
}

void MainWindow::onLogMessage(const QString &message)
{
    appendLog(message);
}

void MainWindow::onServerStarted(quint16 port)
{
    auto &style = MacOSStyleManager::instance();
    m_startStopBtn->setText("Stop");
    // Use a subtle red-tinted button for stop
    m_startStopBtn->setStyleSheet(QString(
        "QPushButton {"
        "    background-color: rgba(255, 69, 58, 0.25);"
        "    color: %1;"
        "    border: none;"
        "    border-radius: 5px;"
        "    padding: 4px 14px;"
        "    font-size: 12px;"
        "    font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "    background-color: rgba(255, 69, 58, 0.35);"
        "}"
        "QPushButton:pressed {"
        "    background-color: rgba(255, 69, 58, 0.2);"
        "}"
    ).arg(style.dangerColor().name()));

    m_portSpinBox->setEnabled(false);
    m_retrySpinBox->setEnabled(false);
    m_cooldownSpinBox->setEnabled(false);
    m_timeoutSpinBox->setEnabled(false);
    m_correctionCheckBox->setEnabled(false);
    m_localTokenCountCheckBox->setEnabled(false);
    m_statusLabel->setText(QString(":%1").arg(port));
    m_statusLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
        .arg(style.successColor().name()));
    updateStatus();
}

void MainWindow::onServerStopped()
{
    auto &style = MacOSStyleManager::instance();
    m_startStopBtn->setText("Start");
    m_startStopBtn->setStyleSheet(style.getAccentButtonStyle());
    m_portSpinBox->setEnabled(true);
    m_retrySpinBox->setEnabled(true);
    m_cooldownSpinBox->setEnabled(true);
    m_timeoutSpinBox->setEnabled(true);
    m_correctionCheckBox->setEnabled(true);
    m_localTokenCountCheckBox->setEnabled(true);
    m_statusLabel->setText("Stopped");
    m_statusLabel->setStyleSheet(QString("font-size: 11px; color: %1;")
        .arg(style.secondaryTextColor().name()));
}

void MainWindow::onUrlSwitched(int index, const QString &url)
{
    Q_UNUSED(index)
    auto &style = MacOSStyleManager::instance();
    m_currentUrlLabel->setText(QString("URL: %1").arg(url));
    m_currentUrlLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    m_needRefreshUrlList = true;
    scheduleListRefresh();
}

void MainWindow::onKeySwitched(int index, const QString &label)
{
    Q_UNUSED(index)
    auto &style = MacOSStyleManager::instance();
    m_currentKeyLabel->setText(QString("Key: %1").arg(label));
    m_currentKeyLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    m_needRefreshKeyList = true;
    scheduleListRefresh();
}

void MainWindow::onUrlItemClicked(QListWidgetItem *item)
{
    if (!item) return;

    int index = m_urlList->row(item);
    if (index >= 0) {
        m_pool->setCurrentUrlIndex(index);
    }
}

void MainWindow::onKeyItemClicked(QListWidgetItem *item)
{
    if (!item) return;

    int index = m_keyList->row(item);
    if (index >= 0) {
        m_pool->setCurrentKeyIndex(index);
    }
}

void MainWindow::refreshCooldowns()
{
    m_pool->refreshCooldowns();
    m_needRefreshUrlList = true;
    m_needRefreshKeyList = true;
    scheduleListRefresh();
}

void MainWindow::updateStatus()
{
    auto &style = MacOSStyleManager::instance();
    QString currentUrl = m_pool->getCurrentUrl();
    QString currentKey = m_pool->getCurrentKey();
    const auto keys = m_pool->getKeys();
    int keyIndex = m_pool->getCurrentKeyIndex();

    m_currentUrlLabel->setText(QString("URL: %1").arg(currentUrl.isEmpty() ? "-" : currentUrl));
    m_currentUrlLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));

    if (!currentKey.isEmpty() && keyIndex >= 0 && keyIndex < keys.size()) {
        m_currentKeyLabel->setText(QString("Key: %1").arg(keys[keyIndex].label));
    } else {
        m_currentKeyLabel->setText("Key: -");
    }
    m_currentKeyLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();

    QMenu *helpMenu = menuBar->addMenu("Help");
    QAction *aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    auto &style = MacOSStyleManager::instance();

    QDialog *aboutDialog = new QDialog(this);
    aboutDialog->setWindowTitle("About Claude Code Balance");
    aboutDialog->setMinimumSize(480, 440);
    aboutDialog->setModal(true);

    // Apply dialog style
    style.applyToDialog(aboutDialog);

    QVBoxLayout *mainLayout = new QVBoxLayout(aboutDialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // Header with icon and title
    QHBoxLayout *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(16);

    QLabel *iconLabel = new QLabel;
    iconLabel->setPixmap(QPixmap(":/img/cc_proxy_blance.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    headerLayout->addWidget(iconLabel);

    QVBoxLayout *titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(4);

    QLabel *titleLabel = new QLabel("Claude Code Balance");
    titleLabel->setStyleSheet(QString("font-size: 20px; font-weight: 600; color: %1;")
        .arg(style.textColor().name()));

    QLabel *versionLabel = new QLabel("Version 1.0");
    versionLabel->setStyleSheet(QString("font-size: 13px; color: %1;")
        .arg(style.secondaryTextColor().name()));

    titleLayout->addWidget(titleLabel);
    titleLayout->addWidget(versionLabel);
    titleLayout->addStretch();
    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Separator
    QFrame *line1 = new QFrame;
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet(QString("background-color: %1;").arg(style.separatorColor().name()));
    line1->setFixedHeight(1);
    mainLayout->addWidget(line1);

    // About info
    QLabel *infoLabel = new QLabel(
        QString("<p style='margin: 0; line-height: 1.6;'>"
        "<b>Author:</b> uk0<br>"
        "<b>GitHub:</b> <a href='https://github.com/uk0' style='color: %1;'>github.com/uk0</a><br>"
        "<b>Blog:</b> <a href='https://firsh.me' style='color: %1;'>firsh.me</a>"
        "</p>"
        "<p style='margin-top: 12px; color: %2;'> "
        "Features:"
        "<br>"
         " • Multi-backend URL/Key management"
                "<br>"
         " • Automatic failover & retry"
                "<br>"
         " • OpenAI ↔ Claude API conversion"
                "<br>"
         " • Streaming (SSE) support"
                "<br>"
         " • Model name mapping"
                "<br>"
         " • Rate limit handling"
                "<br>"
         " • Empty response correction</p>")
        .arg(style.accentColor().name())
        .arg(style.secondaryTextColor().name())
    );
    infoLabel->setTextFormat(Qt::RichText);
    infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    infoLabel->setOpenExternalLinks(true);
    infoLabel->setWordWrap(true);
    mainLayout->addWidget(infoLabel);

    // License section separator
    QFrame *line2 = new QFrame;
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(style.separatorColor().name()));
    line2->setFixedHeight(1);
    mainLayout->addWidget(line2);

    // License status group
    QGroupBox *licenseGroup = new QGroupBox("License Status");
    QVBoxLayout *licenseLayout = new QVBoxLayout(licenseGroup);
    licenseLayout->setSpacing(10);

    QLabel *licenseStatusLabel = new QLabel;
    QLabel *licenseExpirationLabel = new QLabel;

    if (m_licenseManager && m_licenseManager->isLicenseValid()) {
        licenseStatusLabel->setText("Activated");
        licenseStatusLabel->setStyleSheet(QString("font-weight: 600; font-size: 14px; color: %1;")
            .arg(style.successColor().name()));

        int days = m_licenseManager->getDaysRemaining();
        QDate expDate = m_licenseManager->getExpirationDate();
        QString expirationText = QString("Expires: %1 (%2 days remaining)")
                                     .arg(expDate.toString("yyyy-MM-dd"))
                                     .arg(days);
        if (days <= 7) {
            licenseExpirationLabel->setStyleSheet(QString("font-size: 13px; color: %1;")
                .arg(style.warningColor().name()));
        } else {
            licenseExpirationLabel->setStyleSheet(QString("font-size: 13px; color: %1;")
                .arg(style.secondaryTextColor().name()));
        }
        licenseExpirationLabel->setText(expirationText);
    } else {
        licenseStatusLabel->setText("Not activated");
        licenseStatusLabel->setStyleSheet(QString("font-weight: 600; font-size: 14px; color: %1;")
            .arg(style.dangerColor().name()));
        licenseExpirationLabel->setText("No valid license");
        licenseExpirationLabel->setStyleSheet(QString("font-size: 13px; color: %1;")
            .arg(style.secondaryTextColor().name()));
    }

    licenseLayout->addWidget(licenseStatusLabel);
    licenseLayout->addWidget(licenseExpirationLabel);

    // Delete license button
    QPushButton *deleteLicenseBtn = new QPushButton("Delete License");
    deleteLicenseBtn->setStyleSheet(style.getDangerButtonStyle());
    deleteLicenseBtn->setFixedSize(120, 32);

    connect(deleteLicenseBtn, &QPushButton::clicked, aboutDialog, [this, aboutDialog]() {
        QMessageBox::StandardButton reply = QMessageBox::question(
            aboutDialog,
            "Delete License",
            "Are you sure you want to delete the license?\n\nThe application will close and you will need to re-activate.",
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply == QMessageBox::Yes) {
            if (m_licenseManager && m_licenseManager->deleteLicense()) {
                QMessageBox::information(aboutDialog, "Success", "License deleted. The application will now close.");
                aboutDialog->accept();
                QApplication::quit();
            } else {
                QMessageBox::critical(aboutDialog, "Error", "Failed to delete license.");
            }
        }
    });

    QHBoxLayout *deleteBtnLayout = new QHBoxLayout;
    deleteBtnLayout->addStretch();
    deleteBtnLayout->addWidget(deleteLicenseBtn);
    licenseLayout->addLayout(deleteBtnLayout);

    mainLayout->addWidget(licenseGroup);

    // Spacer
    mainLayout->addStretch();

    // Close button
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    QPushButton *closeBtn = new QPushButton("Close");
    closeBtn->setStyleSheet(style.getSecondaryButtonStyle());
    closeBtn->setFixedSize(90, 36);
    connect(closeBtn, &QPushButton::clicked, aboutDialog, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);
    mainLayout->addLayout(buttonLayout);

    aboutDialog->exec();
    aboutDialog->deleteLater();
}

bool MainWindow::checkPermissions()
{
    LOG("MainWindow: Checking permissions...");

    // Check if we can write to config directory
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.ccb";
    }

    QDir dir(configDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            LOG(QString("MainWindow: Cannot create config directory: %1").arg(configDir));
            showPermissionHelp();
            return false;
        }
    }

    // Try to write a test file
    QString testFile = configDir + "/.permission_test";
    QFile file(testFile);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG(QString("MainWindow: Cannot write to config directory: %1").arg(file.errorString()));
        showPermissionHelp();
        return false;
    }
    file.write("test");
    file.close();
    QFile::remove(testFile);

    // Check if we can write to log file
    QFile logFile("/tmp/ccb.log");
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        LOG(QString("MainWindow: Cannot write to log file: %1").arg(logFile.errorString()));
        // This is not critical, just log it
    } else {
        logFile.close();
    }

    LOG("MainWindow: Permission check passed");
    return true;
}

void MainWindow::showPermissionHelp()
{
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Permission Required");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText("<h3>Permission Required</h3>");
    msgBox.setInformativeText(
        "<p>Claude Code Balance needs permission to access:</p>"
        "<ul>"
        "<li>Application Support folder (for config)</li>"
        "<li>Network access (for proxy server)</li>"
        "</ul>"
        "<p><b>To grant permissions:</b></p>"
        "<ol>"
        "<li>Open <b>System Settings</b></li>"
        "<li>Go to <b>Privacy & Security</b></li>"
        "<li>Check <b>Full Disk Access</b> or <b>Files and Folders</b></li>"
        "<li>Add this application</li>"
        "</ol>"
        "<p>Click 'Open Settings' to open System Settings.</p>"
    );
    msgBox.setTextFormat(Qt::RichText);

    QPushButton *settingsBtn = msgBox.addButton("Open Settings", QMessageBox::ActionRole);
    msgBox.addButton("Continue Anyway", QMessageBox::AcceptRole);
    msgBox.addButton(QMessageBox::Cancel);

    msgBox.exec();

    if (msgBox.clickedButton() == settingsBtn) {
        // Open macOS Privacy settings
#ifdef Q_OS_MACOS
        QProcess::startDetached("open", {"x-apple.systempreferences:com.apple.preference.security?Privacy"});
#endif
    } else if (msgBox.result() == QMessageBox::Cancel) {
        // User cancelled - exit the application
        QApplication::quit();
    }
}

void MainWindow::onStatsUpdated(const ProxyServer::Stats &stats)
{
    auto &style = MacOSStyleManager::instance();

    // Format: "Conn: 2 | Req: 15 | OK: 12 | Err: 1 | Cor: 2"
    QString statsText = QString("Conn: %1 | Req: %2 | OK: %3 | Err: %4 | Cor: %5")
                            .arg(stats.activeConnections)
                            .arg(stats.totalRequests)
                            .arg(stats.successCount)
                            .arg(stats.errorCount)
                            .arg(stats.correctionCount);

    m_statsLabel->setText(statsText);

    // Color based on errors/corrections
    if (stats.errorCount > 0) {
        m_statsLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
            .arg(style.warningColor().name()));
    } else if (stats.correctionCount > 0) {
        m_statsLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
            .arg(style.accentColor().name()));
    } else {
        m_statsLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
            .arg(style.secondaryTextColor().name()));
    }
}

void MainWindow::showStyledWarning(const QString &title, const QString &message)
{
    auto &style = MacOSStyleManager::instance();

    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    dialog->setMinimumSize(360, 160);
    dialog->setModal(true);
    style.applyToDialog(dialog);

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    // Warning icon and message
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(16);

    QLabel *iconLabel = new QLabel;
    iconLabel->setText("⚠️");
    iconLabel->setStyleSheet("font-size: 32px;");
    contentLayout->addWidget(iconLabel);

    QLabel *messageLabel = new QLabel(message);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet(QString("font-size: 14px; color: %1;")
        .arg(style.textColor().name()));
    contentLayout->addWidget(messageLabel, 1);

    layout->addLayout(contentLayout);
    layout->addStretch();

    // OK button
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    QPushButton *okBtn = new QPushButton("OK");
    okBtn->setStyleSheet(style.getAccentButtonStyle());
    okBtn->setFixedSize(80, 32);
    connect(okBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonLayout->addWidget(okBtn);

    layout->addLayout(buttonLayout);

    dialog->exec();
    dialog->deleteLater();
}

bool MainWindow::showStyledConfirm(const QString &title, const QString &message, const QString &detail)
{
    auto &style = MacOSStyleManager::instance();

    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    dialog->setMinimumSize(400, 180);
    dialog->setModal(true);
    style.applyToDialog(dialog);

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(24, 24, 24, 24);

    // Warning icon and message
    QHBoxLayout *contentLayout = new QHBoxLayout;
    contentLayout->setSpacing(16);

    QLabel *iconLabel = new QLabel;
    iconLabel->setText("🗑️");
    iconLabel->setStyleSheet("font-size: 32px;");
    contentLayout->addWidget(iconLabel);

    QVBoxLayout *textLayout = new QVBoxLayout;
    textLayout->setSpacing(8);

    QLabel *messageLabel = new QLabel(message);
    messageLabel->setWordWrap(true);
    messageLabel->setStyleSheet(QString("font-size: 14px; font-weight: 600; color: %1;")
        .arg(style.textColor().name()));
    textLayout->addWidget(messageLabel);

    if (!detail.isEmpty()) {
        QLabel *detailLabel = new QLabel(detail);
        detailLabel->setWordWrap(true);
        detailLabel->setStyleSheet(QString("font-size: 12px; color: %1;")
            .arg(style.secondaryTextColor().name()));
        textLayout->addWidget(detailLabel);
    }

    contentLayout->addLayout(textLayout, 1);
    layout->addLayout(contentLayout);
    layout->addStretch();

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    QPushButton *cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(style.getSecondaryButtonStyle());
    cancelBtn->setFixedSize(80, 32);
    connect(cancelBtn, &QPushButton::clicked, dialog, &QDialog::reject);
    buttonLayout->addWidget(cancelBtn);

    QPushButton *confirmBtn = new QPushButton("Delete");
    confirmBtn->setStyleSheet(style.getDangerButtonStyle());
    confirmBtn->setFixedSize(80, 32);
    connect(confirmBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonLayout->addWidget(confirmBtn);

    layout->addLayout(buttonLayout);

    bool result = (dialog->exec() == QDialog::Accepted);
    dialog->deleteLater();
    return result;
}

void MainWindow::showSupportedPlatformsDialog(QWidget *parent)
{
    auto &style = MacOSStyleManager::instance();

    QDialog *dialog = new QDialog(parent);
    dialog->setWindowTitle("Supported Platforms & Models");
    dialog->setMinimumSize(500, 400);
    dialog->setModal(true);
    style.applyToDialog(dialog);

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(24, 24, 24, 24);

    // Title
    QLabel *titleLabel = new QLabel("Supported Third-Party Platforms", dialog);
    titleLabel->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;")
        .arg(style.textColor().name()));
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel *descLabel = new QLabel(
        "The following platforms and their supported models have been tested with this proxy.\n"
        "When using these platforms, enable 'Use OpenAI API Format' and configure model mappings accordingly.", dialog);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("font-size: 12px; color: %1;")
        .arg(style.secondaryTextColor().name()));
    mainLayout->addWidget(descLabel);

    // Platforms list (one-to-many relationship: platform -> models)
    // Structure: { platform_name, platform_url, { model1, model2, ... } }
    struct PlatformInfo {
        QString name;
        QString url;
        QStringList models;
    };

    QVector<PlatformInfo> platforms = {
        {
            "ChatAnywhere",
            "https://api.chatanywhere.tech",
            {
                "gpt-4.1-mini-2025-04-14",
                "gpt-4.1-2025-04-14",
                "gpt-4o-mini",
                "gpt-4o",
                "gpt-4.1-nano",
            }
        },
        {
            "薄荷公益",
            "https://x666.me",
            {
                "gemini-flash-latest"
            }
        }
        // Add more platforms here as needed
    };

    // Create scrollable content
    QScrollArea *scrollArea = new QScrollArea(dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QString("QScrollArea { background: transparent; }"));

    QWidget *scrollContent = new QWidget;
    QVBoxLayout *contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(16);

    for (const PlatformInfo &platform : platforms) {
        QGroupBox *platformGroup = new QGroupBox(platform.name, dialog);
        platformGroup->setStyleSheet(QString(
            "QGroupBox {"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "  color: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 8px;"
            "  margin-top: 12px;"
            "  padding-top: 8px;"
            "}"
            "QGroupBox::title {"
            "  subcontrol-origin: margin;"
            "  left: 12px;"
            "  padding: 0 8px;"
            "}"
        ).arg(style.accentColor().name(), style.borderColor().name()));

        QVBoxLayout *groupLayout = new QVBoxLayout(platformGroup);
        groupLayout->setSpacing(6);

        // URL
        QLabel *urlLabel = new QLabel(QString("URL: %1").arg(platform.url), platformGroup);
        urlLabel->setStyleSheet(QString("font-size: 11px; color: %1; font-weight: normal;")
            .arg(style.secondaryTextColor().name()));
        groupLayout->addWidget(urlLabel);

        // Models
        QLabel *modelsTitle = new QLabel("Supported Models:", platformGroup);
        modelsTitle->setStyleSheet(QString("font-size: 12px; color: %1; font-weight: normal; margin-top: 4px;")
            .arg(style.textColor().name()));
        groupLayout->addWidget(modelsTitle);

        for (const QString &model : platform.models) {
            QLabel *modelLabel = new QLabel(QString("  • %1").arg(model), platformGroup);
            modelLabel->setStyleSheet(QString("font-size: 11px; color: %1; font-weight: normal; font-family: monospace;")
                .arg(style.textColor().name()));
            groupLayout->addWidget(modelLabel);
        }

        contentLayout->addWidget(platformGroup);
    }

    contentLayout->addStretch();
    scrollContent->setLayout(contentLayout);
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Note
    QLabel *noteLabel = new QLabel(
        "Note: Model availability may change. Please check the platform's official documentation for the latest supported models.", dialog);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet(QString("font-size: 11px; color: %1; font-style: italic;")
        .arg(style.secondaryTextColor().name()));
    mainLayout->addWidget(noteLabel);

    // Close button
    QHBoxLayout *buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();

    QPushButton *closeBtn = new QPushButton("Close", dialog);
    closeBtn->setStyleSheet(style.getSecondaryButtonStyle());
    closeBtn->setFixedSize(80, 32);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);

    mainLayout->addLayout(buttonLayout);

    dialog->exec();
    dialog->deleteLater();
}
