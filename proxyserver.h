#ifndef PROXYSERVER_H
#define PROXYSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QByteArray>
#include <QMutex>
#include <QSet>

#include "requesthandler.h"

class BackendPool;
class ConfigManager;

class ProxyServer : public QObject
{
    Q_OBJECT

public:
    explicit ProxyServer(BackendPool *pool, ConfigManager *config, QObject *parent = nullptr);
    ~ProxyServer();

    bool start(quint16 port);
    void stop();
    bool isRunning() const;
    quint16 port() const;

    // Statistics
    struct Stats {
        int activeConnections = 0;
        int totalRequests = 0;
        int successCount = 0;
        int errorCount = 0;
        int correctionCount = 0;
    };

    Stats getStats() const;
    void resetStats();

signals:
    void started(quint16 port);
    void stopped();
    void logMessage(const QString &message);
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    void requestReceived(const QString &method, const QString &path);
    void statsUpdated(const Stats &stats);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onClientError(QAbstractSocket::SocketError error);
    void onRequestCompleted(QTcpSocket *socket);
    void onRequestSuccess();
    void onRequestError();
    void onCorrectionTriggered();

private:
    struct ClientState {
        QByteArray buffer;
        bool headersComplete = false;
        int contentLength = 0;
        int bodyReceived = 0;
        HttpRequest request;
        bool requestInProgress = false;
    };

    bool parseHttpRequest(ClientState &state);
    void processRequest(QTcpSocket *socket, ClientState &state);
    void cleanupSocket(QTcpSocket *socket);

    QTcpServer *m_server;
    BackendPool *m_pool;
    ConfigManager *m_config;
    RequestHandler *m_handler;
    QMap<QTcpSocket*, ClientState> m_clients;
    QSet<QTcpSocket*> m_pendingCleanup;
    mutable QMutex m_mutex;
    bool m_running = false;
    Stats m_stats;
};

#endif // PROXYSERVER_H
