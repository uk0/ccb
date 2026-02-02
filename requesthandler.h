#ifndef REQUESTHANDLER_H
#define REQUESTHANDLER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QUrl>
#include <QByteArray>
#include <QMutex>
#include <QPointer>
#include <QElapsedTimer>
#include <QSet>

#include "conversion/types.h"  // For Conversion::StreamingState

class BackendPool;
class ConfigManager;

struct HttpRequest {
    QString method;
    QString path;
    QString httpVersion;
    QMap<QString, QString> headers;
    QByteArray body;
    QString originalHost;
};

class RequestHandler : public QObject
{
    Q_OBJECT

public:
    explicit RequestHandler(BackendPool *pool, ConfigManager *config, QObject *parent = nullptr);

    void handleRequest(QTcpSocket *clientSocket, const HttpRequest &request);
    void cancelRequestsForSocket(QTcpSocket *socket);

signals:
    void logMessage(const QString &message);
    void requestCompleted(QTcpSocket *socket);
    void requestSuccess();
    void requestError();
    void correctionTriggered();

private slots:
    void onReplyFinished();
    void onReplyReadyRead();
    void onReplyError(QNetworkReply::NetworkError error);
    void onSocketDisconnected();
    void onSocketDestroyed(QObject *obj);

private:
    struct PendingRequest {
        QPointer<QTcpSocket> clientSocket;
        HttpRequest originalRequest;     // Original request - preserved for retry
        int retryCount = 0;
        int maxRetries = 3;
        int usedUrlIndex = -1;
        int usedKeyIndex = -1;
        bool headersSent = false;
        bool streamingStarted = false;   // Once streaming starts, can't retry
        bool cancelled = false;          // Set when client disconnects - stops all processing
        QByteArray accumulatedResponse;  // Accumulate response for null detection
        QElapsedTimer requestTimer;      // Track request duration
        QString originalModelName;       // Original model name from request (for reverse mapping)
        QString mappedModelName;         // Mapped model name sent to backend
        bool useOpenAIFormat = false;    // Whether OpenAI format conversion is enabled
        bool isTokenCountRequest = false; // Whether this is a token count request
        Conversion::StreamingState streamState;  // Per-request streaming state
        bool needsZstdDecompression = false;  // Whether response needs zstd decompression
        QByteArray zstdBuffer;  // Buffer for accumulating zstd compressed data
    };

    void sendRequest(PendingRequest &pending);
    void handleErrorResponse(QNetworkReply *reply, PendingRequest &pending);
    bool shouldRetryOnError(int statusCode, const QByteArray &body);
    bool isRateLimitError(int statusCode, const QByteArray &body);
    bool isQuotaExhaustedError(int statusCode, const QByteArray &body);
    bool isServerError(int statusCode);
    bool isNullResponse(const QByteArray &body);  // Detect null response
    void sendErrorResponse(QTcpSocket *socket, int statusCode, const QString &message);
    void sendResponseHeaders(QTcpSocket *socket, QNetworkReply *reply);
    void sendResponseHeadersWithoutEncoding(QTcpSocket *socket, QNetworkReply *reply);  // For decompressed responses
    bool isSocketValid(const QPointer<QTcpSocket> &socket);  // Acquires lock - use when NOT holding m_mutex
    bool isSocketValidLocked(const QPointer<QTcpSocket> &socket);  // No lock - use when ALREADY holding m_mutex
    QByteArray replaceModelInRequest(const QByteArray &body, QString &originalModel, QString &mappedModel);
    QByteArray replaceModelInResponse(const QByteArray &body, const QString &mappedModel, const QString &originalModel);
    QByteArray decompressZstd(const QByteArray &compressed);  // Decompress zstd data
    bool isZstdCompressed(QNetworkReply *reply);  // Check if response is zstd compressed

    BackendPool *m_pool;
    ConfigManager *m_config;
    QNetworkAccessManager *m_networkManager;
    QMap<QNetworkReply*, PendingRequest> m_pendingRequests;
    QSet<QTcpSocket*> m_disconnectedSockets;  // Track sockets that have disconnected (to handle race conditions)
    mutable QMutex m_mutex;
};

#endif // REQUESTHANDLER_H
