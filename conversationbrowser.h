#ifndef CONVERSATIONBROWSER_H
#define CONVERSATIONBROWSER_H

#include <QDialog>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QSplitter>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QJsonObject>
#include <QJsonArray>

struct SessionEntry {
    QString sessionId;
    QString fullPath;
    QString firstPrompt;
    int messageCount = 0;
    QString created;
    QString modified;
    QString gitBranch;
    QString projectPath;
    qint64 fileSize = 0;
};

struct ContentBlock {
    QString type;       // text, tool_use, tool_result, thinking
    QString text;       // for text/thinking blocks
    QString toolName;   // for tool_use
    QString toolInput;  // for tool_use (summary of input)
    QString toolId;     // for tool_use/tool_result
};

struct ConversationMessage {
    QString type;       // user, assistant
    QString role;
    QString timestamp;
    QString model;
    QList<ContentBlock> blocks;
    QString textContent;// flattened text for export
    int inputTokens = 0;
    int outputTokens = 0;
};

class ConversationBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit ConversationBrowser(QWidget *parent = nullptr);

private slots:
    void onSessionClicked(QTreeWidgetItem *item, int column);
    void onSearchChanged(const QString &text);
    void onExportMarkdown();
    void onExportJson();
    void onRefresh();

private:
    void setupUi();
    void scanProjects();
    void loadHistoryIndex();
    void scanProjectDir(const QString &dirPath, const QString &dirName);
    SessionEntry extractSessionMeta(const QString &jsonlPath, const QString &sessionId);
    QString decodeProjectPath(const QString &dirName);
    void loadSession(const QString &jsonlPath);
    QList<ConversationMessage> parseJsonl(const QString &filePath);
    QList<ContentBlock> parseContentBlocks(const QJsonValue &content);
    QString renderMessages(const QList<ConversationMessage> &messages);
    QString renderBlock(const ContentBlock &block, bool dark);
    QString formatTimestamp(const QString &isoTime);
    QString escapeHtml(const QString &text);
    QString flattenBlocks(const QList<ContentBlock> &blocks);

    QLineEdit *m_searchEdit;
    QTreeWidget *m_sessionTree;
    QTextBrowser *m_messageView;
    QLabel *m_infoLabel;
    QPushButton *m_exportMdBtn;
    QPushButton *m_exportJsonBtn;
    QPushButton *m_refreshBtn;

    QString m_currentSessionPath;
    QList<ConversationMessage> m_currentMessages;
    QMap<QString, QList<SessionEntry>> m_projects; // projectName -> sessions

    // Global history index: sessionId -> {display, project, timestamp}
    struct HistoryEntry {
        QString display;
        QString project;
        qint64 timestamp = 0;
    };
    QMap<QString, HistoryEntry> m_historyIndex;
};

#endif // CONVERSATIONBROWSER_H
