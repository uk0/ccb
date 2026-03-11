#include "conversationbrowser.h"
#include "macosstylemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QDateTime>
#include <QStandardPaths>
#include <QApplication>
#include <QTextStream>
#include <QMessageBox>
#include <QFont>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>

ConversationBrowser::ConversationBrowser(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    scanProjects();
}

void ConversationBrowser::setupUi()
{
    setWindowTitle("Claude Code Conversations");
    resize(1100, 700);
    setMinimumSize(800, 500);

    auto &style = MacOSStyleManager::instance();
    style.applyToDialog(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Toolbar
    QHBoxLayout *toolbarLayout = new QHBoxLayout;
    toolbarLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search conversations...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(28);

    m_refreshBtn = new QPushButton("Refresh", this);
    m_refreshBtn->setFixedSize(90, 28);
    m_refreshBtn->setStyleSheet(style.getSecondaryButtonStyle());

    m_exportMdBtn = new QPushButton("Export Markdown", this);
    m_exportMdBtn->setFixedSize(140, 28);
    m_exportMdBtn->setStyleSheet(style.getAccentButtonStyle());
    m_exportMdBtn->setEnabled(false);

    m_exportJsonBtn = new QPushButton("Export JSON", this);
    m_exportJsonBtn->setFixedSize(110, 28);
    m_exportJsonBtn->setStyleSheet(style.getAccentButtonStyle());
    m_exportJsonBtn->setEnabled(false);

    toolbarLayout->addWidget(m_searchEdit, 1);
    toolbarLayout->addWidget(m_refreshBtn);
    toolbarLayout->addWidget(m_exportMdBtn);
    toolbarLayout->addWidget(m_exportJsonBtn);

    mainLayout->addLayout(toolbarLayout);

    // Splitter: session tree (left) + message view (right)
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);

    // Session tree
    m_sessionTree = new QTreeWidget(this);
    m_sessionTree->setHeaderLabels({"Conversation", "Messages", "Date"});
    m_sessionTree->setRootIsDecorated(true);
    m_sessionTree->setAlternatingRowColors(true);
    m_sessionTree->setMinimumWidth(340);
    m_sessionTree->header()->setStretchLastSection(false);
    m_sessionTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_sessionTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_sessionTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_sessionTree->setIndentation(16);
    // Increase row height for better readability
    m_sessionTree->setStyleSheet(
        QString("QTreeWidget::item { padding: 4px 2px; min-height: 24px; }"
                "QTreeWidget { font-size: 13px; }"));

    // Message view
    m_messageView = new QTextBrowser(this);
    m_messageView->setOpenExternalLinks(false);
    m_messageView->setReadOnly(true);
    QString viewStyle = QString(
        "QTextBrowser { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 8px; font-family: -apple-system, 'SF Pro Text', "
        "'Helvetica Neue', sans-serif; font-size: 13px; }")
        .arg(style.backgroundColor().name(),
             style.textColor().name(),
             style.borderColor().name());
    m_messageView->setStyleSheet(viewStyle);

    splitter->addWidget(m_sessionTree);
    splitter->addWidget(m_messageView);
    splitter->setSizes({350, 750});

    mainLayout->addWidget(splitter, 1);

    // Info bar
    m_infoLabel = new QLabel("", this);
    m_infoLabel->setStyleSheet(QString("color: %1; font-size: 11px;")
        .arg(style.secondaryTextColor().name()));
    mainLayout->addWidget(m_infoLabel);

    // Connections
    connect(m_sessionTree, &QTreeWidget::itemClicked, this, &ConversationBrowser::onSessionClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ConversationBrowser::onSearchChanged);
    connect(m_exportMdBtn, &QPushButton::clicked, this, &ConversationBrowser::onExportMarkdown);
    connect(m_exportJsonBtn, &QPushButton::clicked, this, &ConversationBrowser::onExportJson);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ConversationBrowser::onRefresh);
}

void ConversationBrowser::loadHistoryIndex()
{
    m_historyIndex.clear();
    QString histPath = QDir::homePath() + "/.claude/history.jsonl";
    QFile file(histPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();
        QString sid = obj.value("sessionId").toString();
        if (sid.isEmpty()) continue;
        HistoryEntry he;
        he.display = obj.value("display").toString();
        he.project = obj.value("project").toString();
        he.timestamp = static_cast<qint64>(obj.value("timestamp").toDouble());
        // Keep latest entry per session
        if (!m_historyIndex.contains(sid) || he.timestamp > m_historyIndex[sid].timestamp)
            m_historyIndex[sid] = he;
    }
    file.close();
}

QString ConversationBrowser::decodeProjectPath(const QString &dirName)
{
    // Convert encoded dir name back to path: -Users-firshme-Desktop → /Users/firshme/Desktop
    QString path = dirName;
    path.replace('-', '/');
    // Handle drive letters on Windows (e.g., /C//path → C:/path)
    if (path.length() > 2 && path[0] == '/' && path[2] == '/') {
        path = path.mid(1, 1) + ":" + path.mid(2);
    }
    return path;
}

SessionEntry ConversationBrowser::extractSessionMeta(
    const QString &jsonlPath, const QString &sessionId)
{
    SessionEntry entry;
    entry.sessionId = sessionId;
    entry.fullPath = jsonlPath;

    QFileInfo fi(jsonlPath);
    entry.fileSize = fi.size();
    entry.modified = fi.lastModified().toUTC().toString(Qt::ISODateWithMs);
    entry.created = fi.birthTime().isValid()
        ? fi.birthTime().toUTC().toString(Qt::ISODateWithMs)
        : entry.modified;

    // Use history index for display text if available
    if (m_historyIndex.contains(sessionId)) {
        entry.firstPrompt = m_historyIndex[sessionId].display;
    }

    // Quick scan: read first ~100 lines to get first user prompt and count messages
    QFile file(jsonlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    int msgCount = 0;
    int linesRead = 0;
    bool foundPrompt = entry.firstPrompt.isEmpty() ? false : true;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        linesRead++;

        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();
        QString type = obj.value("type").toString();

        if (type == "user" || type == "assistant")
            msgCount++;

        // Extract first user prompt if not from history
        if (!foundPrompt && type == "user") {
            QJsonObject msgObj = obj.value("message").toObject();
            QJsonValue content = msgObj.value("content");
            if (content.isString()) {
                entry.firstPrompt = content.toString().left(200);
                foundPrompt = true;
            } else if (content.isArray()) {
                for (const QJsonValue &v : content.toArray()) {
                    if (v.isObject() && v.toObject().value("type").toString() == "text") {
                        entry.firstPrompt = v.toObject().value("text").toString().left(200);
                        foundPrompt = true;
                        break;
                    }
                }
            }
        }

        // After 200 lines, estimate remaining messages
        if (linesRead >= 200) {
            qint64 bytesRead = file.pos();
            if (bytesRead > 0) {
                double ratio = static_cast<double>(fi.size()) / bytesRead;
                msgCount = static_cast<int>(msgCount * ratio);
            }
            break;
        }
    }
    file.close();
    entry.messageCount = msgCount;
    return entry;
}

void ConversationBrowser::scanProjectDir(const QString &dirPath, const QString &dirName)
{
    QDir dir(dirPath);
    QString home = QDir::homePath();

    // Try to get display name from sessions-index.json first
    QString displayName;
    QMap<QString, SessionEntry> indexEntries; // sessionId -> entry from index

    QString indexPath = dirPath + "/sessions-index.json";
    QFile indexFile(indexPath);
    if (indexFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(indexFile.readAll());
        indexFile.close();
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            displayName = root.value("originalPath").toString();
            QJsonArray entries = root.value("entries").toArray();
            for (const QJsonValue &val : entries) {
                QJsonObject obj = val.toObject();
                SessionEntry se;
                se.sessionId = obj.value("sessionId").toString();
                se.fullPath = obj.value("fullPath").toString();
                se.firstPrompt = obj.value("firstPrompt").toString();
                se.messageCount = obj.value("messageCount").toInt();
                se.created = obj.value("created").toString();
                se.modified = obj.value("modified").toString();
                se.gitBranch = obj.value("gitBranch").toString();
                if (se.fullPath.isEmpty())
                    se.fullPath = dirPath + "/" + se.sessionId + ".jsonl";
                if (!se.sessionId.isEmpty())
                    indexEntries[se.sessionId] = se;
            }
        }
    }

    // Decode directory name as fallback display name
    if (displayName.isEmpty())
        displayName = decodeProjectPath(dirName);

    // Shorten home path for display
    if (displayName.startsWith(home))
        displayName = "~" + displayName.mid(home.length());

    // Scan all .jsonl files in the directory
    QStringList jsonlFiles = dir.entryList({"*.jsonl"}, QDir::Files);
    QSet<QString> discoveredIds;
    QList<SessionEntry> sessions;

    for (const QString &fileName : jsonlFiles) {
        QString sessionId = fileName.chopped(6); // remove .jsonl
        discoveredIds.insert(sessionId);
        QString fullPath = dirPath + "/" + fileName;

        // Use index entry if available, otherwise extract from file
        if (indexEntries.contains(sessionId)) {
            SessionEntry se = indexEntries[sessionId];
            se.projectPath = displayName;
            // Enrich with history display if prompt is empty
            if (se.firstPrompt.isEmpty() && m_historyIndex.contains(sessionId))
                se.firstPrompt = m_historyIndex[sessionId].display;
            // Get file size
            QFileInfo fi(fullPath);
            se.fileSize = fi.size();
            if (se.modified.isEmpty())
                se.modified = fi.lastModified().toUTC().toString(Qt::ISODateWithMs);
            sessions.append(se);
        } else {
            SessionEntry se = extractSessionMeta(fullPath, sessionId);
            se.projectPath = displayName;
            sessions.append(se);
        }
    }

    if (sessions.isEmpty()) return;

    // Sort by modified time descending
    std::sort(sessions.begin(), sessions.end(), [](const SessionEntry &a, const SessionEntry &b) {
        return a.modified > b.modified;
    });

    m_projects[displayName] = sessions;
}

void ConversationBrowser::scanProjects()
{
    m_projects.clear();
    m_sessionTree->clear();

    // Load global history index first
    loadHistoryIndex();

    QString claudeDir = QDir::homePath() + "/.claude/projects";
    QDir projectsDir(claudeDir);
    if (!projectsDir.exists()) {
        m_infoLabel->setText("No Claude Code data found at ~/.claude/projects");
        return;
    }

    int totalSessions = 0;
    QStringList projectDirs = projectsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &projDir : projectDirs) {
        scanProjectDir(claudeDir + "/" + projDir, projDir);
    }

    // Count total sessions
    for (auto it = m_projects.begin(); it != m_projects.end(); ++it)
        totalSessions += it.value().size();

    // Populate tree
    auto &style = MacOSStyleManager::instance();
    Q_UNUSED(style);

    // Sort projects by most recent session
    QList<QPair<QString, QList<SessionEntry>>> sortedProjects;
    for (auto it = m_projects.begin(); it != m_projects.end(); ++it) {
        sortedProjects.append({it.key(), it.value()});
    }
    std::sort(sortedProjects.begin(), sortedProjects.end(),
        [](const QPair<QString, QList<SessionEntry>> &a, const QPair<QString, QList<SessionEntry>> &b) {
            QString aTime = a.second.isEmpty() ? "" : a.second.first().modified;
            QString bTime = b.second.isEmpty() ? "" : b.second.first().modified;
            return aTime > bTime;
        });

    for (const auto &pair : sortedProjects) {
        QTreeWidgetItem *projItem = new QTreeWidgetItem(m_sessionTree);
        projItem->setText(0, pair.first);
        projItem->setText(1, QString::number(pair.second.size()));
        if (!pair.second.isEmpty())
            projItem->setText(2, formatTimestamp(pair.second.first().modified));
        projItem->setFlags(projItem->flags() & ~Qt::ItemIsSelectable);

        QFont boldFont = projItem->font(0);
        boldFont.setBold(true);
        projItem->setFont(0, boldFont);

        for (const SessionEntry &session : pair.second) {
            QTreeWidgetItem *sessItem = new QTreeWidgetItem(projItem);
            QString prompt = session.firstPrompt.left(80);
            if (session.firstPrompt.length() > 80) prompt += "...";
            if (prompt.isEmpty()) prompt = "(empty)";
            sessItem->setText(0, prompt);
            sessItem->setText(1, QString::number(session.messageCount));
            sessItem->setText(2, formatTimestamp(session.modified));
            sessItem->setData(0, Qt::UserRole, session.fullPath);
            sessItem->setToolTip(0, session.firstPrompt);
        }
    }

    m_sessionTree->expandAll();
    m_infoLabel->setText(QString("Found %1 projects, %2 conversations")
        .arg(m_projects.size()).arg(totalSessions));
}

void ConversationBrowser::onSessionClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;

    loadSession(path);
}

void ConversationBrowser::loadSession(const QString &jsonlPath)
{
    m_currentSessionPath = jsonlPath;
    m_currentMessages = parseJsonl(jsonlPath);

    if (m_currentMessages.isEmpty()) {
        m_messageView->setHtml("<p style='color:gray;'>No messages found in this session.</p>");
        m_exportMdBtn->setEnabled(false);
        m_exportJsonBtn->setEnabled(false);
        return;
    }

    m_messageView->setHtml(renderMessages(m_currentMessages));
    m_exportMdBtn->setEnabled(true);
    m_exportJsonBtn->setEnabled(true);

    m_infoLabel->setText(QString("Loaded %1 messages from session").arg(m_currentMessages.size()));
}

QList<ConversationMessage> ConversationBrowser::parseJsonl(const QString &filePath)
{
    QList<ConversationMessage> messages;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return messages;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        QJsonObject obj = doc.object();
        QString type = obj.value("type").toString();

        // Only process user and assistant messages
        if (type != "user" && type != "assistant")
            continue;

        // Skip sidechain messages
        if (obj.value("isSidechain").toBool(false))
            continue;

        QJsonObject messageObj = obj.value("message").toObject();
        QJsonValue contentVal = messageObj.value("content");

        // Parse content blocks
        QList<ContentBlock> blocks = parseContentBlocks(contentVal);

        // Skip user messages that only contain tool_result (internal tool feedback)
        if (type == "user" && !blocks.isEmpty()) {
            bool allToolResults = true;
            for (const ContentBlock &b : blocks) {
                if (b.type != "tool_result") { allToolResults = false; break; }
            }
            if (allToolResults) continue;
        }

        // Skip messages with no meaningful content
        if (blocks.isEmpty()) continue;

        ConversationMessage msg;
        msg.type = type;
        msg.role = messageObj.value("role").toString();
        msg.timestamp = obj.value("timestamp").toString();
        msg.blocks = blocks;
        msg.textContent = flattenBlocks(blocks);

        if (type == "assistant") {
            msg.model = messageObj.value("model").toString();
            QJsonObject usage = messageObj.value("usage").toObject();
            msg.inputTokens = usage.value("input_tokens").toInt();
            msg.outputTokens = usage.value("output_tokens").toInt();
        }

        messages.append(msg);
    }

    file.close();
    return messages;
}

QList<ContentBlock> ConversationBrowser::parseContentBlocks(const QJsonValue &content)
{
    QList<ContentBlock> blocks;

    if (content.isString()) {
        QString text = content.toString().trimmed();
        if (!text.isEmpty()) {
            ContentBlock b;
            b.type = "text";
            b.text = text;
            blocks.append(b);
        }
        return blocks;
    }

    if (!content.isArray()) return blocks;

    for (const QJsonValue &val : content.toArray()) {
        if (val.isString()) {
            QString text = val.toString().trimmed();
            if (!text.isEmpty()) {
                ContentBlock b;
                b.type = "text";
                b.text = text;
                blocks.append(b);
            }
            continue;
        }
        if (!val.isObject()) continue;

        QJsonObject obj = val.toObject();
        QString blockType = obj.value("type").toString();

        if (blockType == "text") {
            QString text = obj.value("text").toString().trimmed();
            if (!text.isEmpty()) {
                ContentBlock b;
                b.type = "text";
                b.text = text;
                blocks.append(b);
            }
        } else if (blockType == "tool_use") {
            ContentBlock b;
            b.type = "tool_use";
            b.toolName = obj.value("name").toString();
            b.toolId = obj.value("id").toString();
            // Summarize input
            QJsonObject input = obj.value("input").toObject();
            QStringList params;
            for (auto it = input.begin(); it != input.end(); ++it) {
                QString val = it.value().isString() ? it.value().toString() : QString();
                if (val.length() > 60) val = val.left(60) + "...";
                if (!val.isEmpty())
                    params.append(it.key() + ": " + val);
            }
            b.toolInput = params.join(", ");
            blocks.append(b);
        } else if (blockType == "thinking") {
            QString thinking = obj.value("thinking").toString().trimmed();
            if (!thinking.isEmpty()) {
                ContentBlock b;
                b.type = "thinking";
                b.text = thinking;
                blocks.append(b);
            }
        } else if (blockType == "tool_result") {
            ContentBlock b;
            b.type = "tool_result";
            b.toolId = obj.value("tool_use_id").toString();
            QJsonValue rc = obj.value("content");
            if (rc.isString()) {
                b.text = rc.toString();
                if (b.text.length() > 300)
                    b.text = b.text.left(300) + "...";
            }
            blocks.append(b);
        }
    }
    return blocks;
}

QString ConversationBrowser::flattenBlocks(const QList<ContentBlock> &blocks)
{
    QStringList parts;
    for (const ContentBlock &b : blocks) {
        if (b.type == "text")
            parts.append(b.text);
        else if (b.type == "tool_use")
            parts.append(QString("[Tool: %1] %2").arg(b.toolName, b.toolInput));
        else if (b.type == "thinking")
            parts.append(QString("[Thinking] %1").arg(b.text.left(200)));
    }
    return parts.join("\n\n");
}

QString ConversationBrowser::renderBlock(const ContentBlock &block, bool dark)
{
    QString codeBg = dark ? "#1a1a1a" : "#e8e8e8";
    QString toolBg = dark ? "#1e2a1e" : "#e8f5e9";
    QString toolBorder = dark ? "#4caf50" : "#66bb6a";
    QString thinkBg = dark ? "#2a2a1e" : "#fff8e1";
    QString thinkBorder = dark ? "#ffa726" : "#ffb74d";
    QString metaColor = dark ? "#888" : "#999";

    if (block.type == "text") {
        QString text = escapeHtml(block.text);
        // Code blocks: ```lang\n...\n```
        static QRegularExpression codeBlockRe("```(\\w*)\\n([\\s\\S]*?)```");
        text.replace(codeBlockRe,
            QString("<pre style='background:%1;padding:8px;border-radius:6px;"
                    "font-family:SF Mono,Menlo,monospace;font-size:12px;"
                    "margin:6px 0;overflow-x:auto;'>\\2</pre>").arg(codeBg));
        // Inline code
        static QRegularExpression inlineCodeRe("`([^`]+)`");
        text.replace(inlineCodeRe,
            QString("<code style='background:%1;padding:1px 4px;border-radius:3px;"
                    "font-family:SF Mono,Menlo,monospace;font-size:12px;'>\\1</code>").arg(codeBg));
        return QString("<div style='white-space:pre-wrap;word-wrap:break-word;line-height:1.5;'>%1</div>").arg(text);
    }

    if (block.type == "tool_use") {
        QString input = escapeHtml(block.toolInput);
        return QString(
            "<div style='background:%1;border-left:3px solid %2;border-radius:4px;"
            "padding:6px 10px;margin:6px 0;font-size:12px;'>"
            "<span style='font-weight:600;'>&#128295; %3</span>"
            "<span style='color:%4;margin-left:8px;'>%5</span>"
            "</div>")
            .arg(toolBg, toolBorder, escapeHtml(block.toolName), metaColor, input);
    }

    if (block.type == "thinking") {
        QString text = escapeHtml(block.text);
        if (text.length() > 500) text = text.left(500) + "...";
        return QString(
            "<div style='background:%1;border-left:3px solid %2;border-radius:4px;"
            "padding:6px 10px;margin:6px 0;font-size:12px;font-style:italic;'>"
            "<span style='font-weight:600;'>&#128161; Thinking</span><br/>%3"
            "</div>")
            .arg(thinkBg, thinkBorder, text);
    }

    return QString();
}

QString ConversationBrowser::renderMessages(const QList<ConversationMessage> &messages)
{
    auto &style = MacOSStyleManager::instance();
    bool dark = style.isDarkMode();

    QString userBg = dark ? "#1a3a5c" : "#e3f2fd";
    QString assistantBg = dark ? "#2d2d2d" : "#f5f5f5";
    QString userColor = dark ? "#90caf9" : "#1565c0";
    QString assistantColor = dark ? "#81c784" : "#2e7d32";
    QString textColor = style.textColor().name();
    QString metaColor = style.secondaryTextColor().name();

    QString html = QString(
        "<html><head><style>"
        "body { font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', sans-serif; "
        "       font-size: 13px; color: %1; margin: 0; padding: 0; }"
        "</style></head><body>")
        .arg(textColor);

    for (const ConversationMessage &msg : messages) {
        bool isUser = (msg.type == "user");
        QString bg = isUser ? userBg : assistantBg;
        QString roleColor = isUser ? userColor : assistantColor;
        QString roleLabel = isUser ? "&#128100; User" : "&#129302; Assistant";

        html += QString("<div style='margin:8px 4px;padding:10px 14px;border-radius:8px;"
                        "background:%1;border-left:3px solid %2;'>").arg(bg, roleColor);

        // Role header
        html += QString("<div style='font-weight:600;font-size:12px;color:%1;margin-bottom:6px;'>%2</div>")
            .arg(roleColor, roleLabel);

        // Render each content block
        for (const ContentBlock &block : msg.blocks) {
            html += renderBlock(block, dark);
        }

        // Meta info line
        QStringList meta;
        if (!msg.timestamp.isEmpty())
            meta.append(formatTimestamp(msg.timestamp));
        if (!msg.model.isEmpty())
            meta.append(msg.model);
        if (msg.outputTokens > 0)
            meta.append(QString("%1 in / %2 out tokens").arg(msg.inputTokens).arg(msg.outputTokens));

        if (!meta.isEmpty())
            html += QString("<div style='font-size:11px;color:%1;margin-top:8px;'>%2</div>")
                .arg(metaColor, meta.join(" &middot; "));

        html += "</div>";
    }

    html += "</body></html>";
    return html;
}

QString ConversationBrowser::formatTimestamp(const QString &isoTime)
{
    QDateTime dt = QDateTime::fromString(isoTime, Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(isoTime, Qt::ISODate);
    if (!dt.isValid())
        return isoTime;
    return dt.toLocalTime().toString("yyyy-MM-dd HH:mm");
}

QString ConversationBrowser::escapeHtml(const QString &text)
{
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    return escaped;
}

void ConversationBrowser::onSearchChanged(const QString &text)
{
    QString filter = text.trimmed().toLower();

    for (int i = 0; i < m_sessionTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *projItem = m_sessionTree->topLevelItem(i);
        bool anyChildVisible = false;

        for (int j = 0; j < projItem->childCount(); ++j) {
            QTreeWidgetItem *sessItem = projItem->child(j);
            bool match = filter.isEmpty()
                || sessItem->text(0).toLower().contains(filter)
                || projItem->text(0).toLower().contains(filter);
            sessItem->setHidden(!match);
            if (match) anyChildVisible = true;
        }

        projItem->setHidden(!anyChildVisible && !filter.isEmpty());
        if (anyChildVisible) projItem->setExpanded(true);
    }
}

void ConversationBrowser::onRefresh()
{
    scanProjects();
    m_messageView->clear();
    m_exportMdBtn->setEnabled(false);
    m_exportJsonBtn->setEnabled(false);
    m_currentMessages.clear();
    m_currentSessionPath.clear();
}

void ConversationBrowser::onExportMarkdown()
{
    if (m_currentMessages.isEmpty()) return;

    QString defaultName = "conversation-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".md";
    QString filePath = QFileDialog::getSaveFileName(this, "Export as Markdown",
        QDir::homePath() + "/" + defaultName, "Markdown (*.md)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export Failed", "Could not write to file.");
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Claude Code Conversation\n\n";
    out << "Exported: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n\n";
    out << "---\n\n";

    for (const ConversationMessage &msg : m_currentMessages) {
        bool isUser = (msg.type == "user");
        out << "## " << (isUser ? "User" : "Assistant") << "\n\n";

        if (!msg.timestamp.isEmpty())
            out << "*" << formatTimestamp(msg.timestamp) << "*";
        if (!msg.model.isEmpty())
            out << " · *" << msg.model << "*";
        if (msg.outputTokens > 0)
            out << " · *tokens: " << msg.inputTokens << " in / " << msg.outputTokens << " out*";
        out << "\n\n";

        out << msg.textContent << "\n\n";
        out << "---\n\n";
    }

    file.close();
    m_infoLabel->setText("Exported to: " + filePath);
}

void ConversationBrowser::onExportJson()
{
    if (m_currentMessages.isEmpty()) return;

    QString defaultName = "conversation-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".json";
    QString filePath = QFileDialog::getSaveFileName(this, "Export as JSON",
        QDir::homePath() + "/" + defaultName, "JSON (*.json)");
    if (filePath.isEmpty()) return;

    QJsonArray arr;
    for (const ConversationMessage &msg : m_currentMessages) {
        QJsonObject obj;
        obj["type"] = msg.type;
        obj["role"] = msg.role;
        obj["timestamp"] = msg.timestamp;
        obj["content"] = msg.textContent;
        if (!msg.model.isEmpty()) obj["model"] = msg.model;
        if (msg.inputTokens > 0) obj["input_tokens"] = msg.inputTokens;
        if (msg.outputTokens > 0) obj["output_tokens"] = msg.outputTokens;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Export Failed", "Could not write to file.");
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    m_infoLabel->setText("Exported to: " + filePath);
}
