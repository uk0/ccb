#include "proxyserver.h"
#include "backendpool.h"
#include "configmanager.h"
#include "logger.h"

#include <QHostAddress>
#include <QMutexLocker>

ProxyServer::ProxyServer(BackendPool *pool, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_pool(pool)
    , m_config(config)
    , m_handler(new RequestHandler(pool, config, this))
{
    connect(m_server, &QTcpServer::newConnection, this, &ProxyServer::onNewConnection);
    connect(m_handler, &RequestHandler::logMessage, this, &ProxyServer::logMessage);
    connect(m_handler, &RequestHandler::requestCompleted, this, &ProxyServer::onRequestCompleted);
    connect(m_handler, &RequestHandler::requestSuccess, this, &ProxyServer::onRequestSuccess);
    connect(m_handler, &RequestHandler::requestError, this, &ProxyServer::onRequestError);
    connect(m_handler, &RequestHandler::correctionTriggered, this, &ProxyServer::onCorrectionTriggered);
}

ProxyServer::~ProxyServer()
{
    stop();
}

bool ProxyServer::start(quint16 port)
{
    QMutexLocker locker(&m_mutex);

    if (m_running) {
        locker.unlock();
        stop();
        locker.relock();
    }

    if (!m_server->listen(QHostAddress::Any, port)) {
        emit logMessage(QString("Failed to start server: %1").arg(m_server->errorString()));
        return false;
    }

    m_running = true;
    emit logMessage(QString("Proxy server started on port %1").arg(port));
    emit started(port);
    return true;
}

void ProxyServer::stop()
{
    QList<QTcpSocket*> socketsToClose;

    {
        QMutexLocker locker(&m_mutex);
        if (!m_running) return;

        // Collect sockets to close
        socketsToClose = m_clients.keys();
        m_clients.clear();
        m_pendingCleanup.clear();
        m_running = false;
    }
    // Lock released before closing sockets

    // Close all client connections without holding the lock
    for (QTcpSocket *socket : socketsToClose) {
        if (socket) {
            socket->disconnect(this);
            m_handler->cancelRequestsForSocket(socket);
            socket->close();
            socket->deleteLater();
        }
    }

    m_server->close();

    emit logMessage("Proxy server stopped");
    emit stopped();
}

bool ProxyServer::isRunning() const
{
    QMutexLocker locker(&m_mutex);
    return m_running;
}

quint16 ProxyServer::port() const
{
    return m_server->serverPort();
}

void ProxyServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket) continue;

        QString clientAddr = socket->peerAddress().toString();
        LOG(QString("ProxyServer: New connection from %1").arg(clientAddr));
        // Reduce UI log frequency - don't emit for every connection
        // emit logMessage(QString("Client connected: %1").arg(clientAddr));
        emit clientConnected(clientAddr);

        {
            QMutexLocker locker(&m_mutex);
            m_clients[socket] = ClientState();
        }

        connect(socket, &QTcpSocket::readyRead, this, &ProxyServer::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ProxyServer::onClientDisconnected);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
                this, &ProxyServer::onClientError);

        // Emit updated stats
        emit statsUpdated(getStats());
    }
}

void ProxyServer::onClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QMutexLocker locker(&m_mutex);
    if (!m_clients.contains(socket)) return;

    ClientState &state = m_clients[socket];

    // Don't process more data if a request is already being handled
    if (state.requestInProgress) {
        return;
    }

    state.buffer.append(socket->readAll());

    // Limit buffer size to prevent memory exhaustion
    const int MAX_BUFFER_SIZE = 100 * 1024 * 1024; // 100MB
    if (state.buffer.size() > MAX_BUFFER_SIZE) {
        emit logMessage("Error: request too large, closing connection");
        locker.unlock();
        cleanupSocket(socket);
        return;
    }

    // Try to parse HTTP request
    if (!state.headersComplete) {
        if (!parseHttpRequest(state)) {
            return; // Need more data
        }
    }

    // Check if we have complete body
    if (state.contentLength > 0) {
        int headersEnd = state.buffer.indexOf("\r\n\r\n");
        if (headersEnd >= 0) {
            int bodyStart = headersEnd + 4;
            int bodySize = state.buffer.size() - bodyStart;
            if (bodySize < state.contentLength) {
                return; // Need more body data
            }
            state.request.body = state.buffer.mid(bodyStart, state.contentLength);
        }
    }

    // Mark request as in progress before processing
    state.requestInProgress = true;
    locker.unlock();

    // Process complete request
    processRequest(socket, state);
}

void ProxyServer::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString clientAddr;
    bool shouldCleanup = false;

    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingCleanup.contains(socket)) {
            m_pendingCleanup.remove(socket);
            m_clients.remove(socket);
            socket->deleteLater();
            return;
        }
        clientAddr = socket->peerAddress().toString();
        shouldCleanup = m_clients.contains(socket);
    }
    // Lock released before calling handler

    LOG(QString("ProxyServer: Client disconnected: %1").arg(clientAddr));
    emit clientDisconnected(clientAddr);

    // Cancel any pending requests for this socket (without holding our lock)
    m_handler->cancelRequestsForSocket(socket);

    if (shouldCleanup) {
        cleanupSocket(socket);
    }

    // Emit updated stats
    emit statsUpdated(getStats());
}

void ProxyServer::onClientError(QAbstractSocket::SocketError error)
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Only log to file, not UI
    LOG(QString("ProxyServer: Socket error: %1 - %2").arg(error).arg(socket->errorString()));
    // Don't emit to UI for common errors
    // emit logMessage(QString("Socket error: %1 - %2").arg(error).arg(socket->errorString()));
}

void ProxyServer::onRequestCompleted(QTcpSocket *socket)
{
    if (!socket) return;

    {
        QMutexLocker locker(&m_mutex);
        if (m_clients.contains(socket)) {
            m_clients[socket].requestInProgress = false;
        }
    }

    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
    }
}

void ProxyServer::cleanupSocket(QTcpSocket *socket)
{
    if (!socket) return;

    QMutexLocker locker(&m_mutex);
    m_clients.remove(socket);
    m_pendingCleanup.remove(socket);
    socket->deleteLater();
}

bool ProxyServer::parseHttpRequest(ClientState &state)
{
    int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false; // Headers not complete
    }

    QString headerSection = QString::fromUtf8(state.buffer.left(headerEnd));
    QStringList lines = headerSection.split("\r\n");

    if (lines.isEmpty()) {
        emit logMessage("Error: empty request");
        return false;
    }

    // Parse request line
    QStringList requestLine = lines[0].split(' ');
    if (requestLine.size() < 3) {
        emit logMessage("Error: invalid request line");
        return false;
    }

    state.request.method = requestLine[0];
    state.request.path = requestLine[1];
    state.request.httpVersion = requestLine[2];

    // Validate method
    QStringList validMethods = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
    if (!validMethods.contains(state.request.method.toUpper())) {
        emit logMessage(QString("Warning: unusual HTTP method: %1").arg(state.request.method));
    }

    // Parse headers
    for (int i = 1; i < lines.size(); ++i) {
        int colonPos = lines[i].indexOf(':');
        if (colonPos > 0) {
            QString name = lines[i].left(colonPos).trimmed();
            QString value = lines[i].mid(colonPos + 1).trimmed();
            state.request.headers[name] = value;

            if (name.toLower() == "content-length") {
                bool ok;
                state.contentLength = value.toInt(&ok);
                if (!ok || state.contentLength < 0) {
                    emit logMessage("Error: invalid Content-Length");
                    state.contentLength = 0;
                }
            } else if (name.toLower() == "host") {
                state.request.originalHost = value;
            }
        }
    }

    state.headersComplete = true;

    // If no body expected, we're done
    if (state.contentLength == 0) {
        return true;
    }

    // Check if we have complete body
    int bodyStart = headerEnd + 4;
    return state.buffer.size() >= bodyStart + state.contentLength;
}

void ProxyServer::processRequest(QTcpSocket *socket, ClientState &state)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        emit logMessage("Error: socket invalid in processRequest");
        return;
    }

    // Increment total requests
    {
        QMutexLocker locker(&m_mutex);
        m_stats.totalRequests++;
    }
    emit statsUpdated(getStats());

    emit requestReceived(state.request.method, state.request.path);
    emit logMessage(QString("Request: %1 %2").arg(state.request.method, state.request.path));

    // Forward to handler
    m_handler->handleRequest(socket, state.request);

    // Reset state for potential keep-alive (though we use Connection: close)
    {
        QMutexLocker locker(&m_mutex);
        if (m_clients.contains(socket)) {
            ClientState newState;
            newState.requestInProgress = true; // Keep this true until request completes
            m_clients[socket] = newState;
        }
    }
}

ProxyServer::Stats ProxyServer::getStats() const
{
    QMutexLocker locker(&m_mutex);
    Stats stats = m_stats;
    stats.activeConnections = m_clients.size();
    return stats;
}

void ProxyServer::resetStats()
{
    QMutexLocker locker(&m_mutex);
    m_stats = Stats();
    m_stats.activeConnections = m_clients.size();
}

void ProxyServer::onRequestSuccess()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stats.successCount++;
    }
    emit statsUpdated(getStats());
}

void ProxyServer::onRequestError()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stats.errorCount++;
    }
    emit statsUpdated(getStats());
}

void ProxyServer::onCorrectionTriggered()
{
    {
        QMutexLocker locker(&m_mutex);
        m_stats.correctionCount++;
    }
    emit statsUpdated(getStats());
}
