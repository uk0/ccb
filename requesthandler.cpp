#include "requesthandler.h"
#include "backendpool.h"
#include "configmanager.h"
#include "conversion/request_converter.h"
#include "conversion/response_converter.h"
#include "conversion/streaming_converter.h"
#include "logger.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QSslConfiguration>

#ifdef HAVE_ZSTD
#include <zstd.h>
#endif

RequestHandler::RequestHandler(BackendPool *pool, ConfigManager *config, QObject *parent)
    : QObject(parent)
    , m_pool(pool)
    , m_config(config)
    , m_networkManager(new QNetworkAccessManager(this))
{
    // Configure network manager for better compatibility
    m_networkManager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

bool RequestHandler::isSocketValid(const QPointer<QTcpSocket> &socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    // Also check if socket is in the disconnected set (handles race conditions)
    QMutexLocker locker(&m_mutex);
    return !m_disconnectedSockets.contains(socket.data());
}

// Version for use when caller already holds m_mutex - prevents deadlock
bool RequestHandler::isSocketValidLocked(const QPointer<QTcpSocket> &socket)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    // Caller must already hold m_mutex
    return !m_disconnectedSockets.contains(socket.data());
}

void RequestHandler::handleRequest(QTcpSocket *clientSocket, const HttpRequest &request)
{
    LOG("=============================================");
    LOG(QString("RequestHandler: NEW REQUEST - %1 %2").arg(request.method, request.path));
    LOG(QString("RequestHandler: Request body size: %1 bytes").arg(request.body.size()));
    LOG("=============================================");

    if (!clientSocket) {
        LOG("RequestHandler: Error - null client socket");
        emit logMessage("Error: null client socket");
        return;
    }

    if (!m_pool || !m_config) {
        LOG("RequestHandler: Error - pool or config is null");
        emit logMessage("Error: pool or config is null");
        sendErrorResponse(clientSocket, 500, "Internal server error");
        return;
    }

    if (!m_pool->hasAvailableBackend()) {
        LOG("RequestHandler: No available backend");
        emit logMessage("No available backend!");
        sendErrorResponse(clientSocket, 503, "No available backend");
        return;
    }

    // Check for local token count handling (OpenAI mode + local token count enabled + count_tokens endpoint)
    if (m_pool->isOpenAIFormatEnabled() &&
        m_config->localTokenCount() &&
        Conversion::RequestConverter::isTokenCountEndpoint(request.path)) {
        // Handle locally without API call - much faster
        int tokenCount = Conversion::RequestConverter::estimateTokenCount(request.body);
        QByteArray response = Conversion::ResponseConverter::createLocalTokenCountResponse(tokenCount);

        QString httpResponse = QString("HTTP/1.1 200 OK\r\n"
                                        "Content-Type: application/json\r\n"
                                        "Content-Length: %1\r\n"
                                        "Connection: close\r\n"
                                        "\r\n").arg(response.size());
        clientSocket->write(httpResponse.toUtf8());
        clientSocket->write(response);
        clientSocket->flush();

        emit logMessage(QString("[Local Token Count] Estimated tokens: %1")
                            .arg(QString::fromUtf8(response)));
        emit requestCompleted(clientSocket);
        emit requestSuccess();
        return;
    }

    // Connect to socket disconnect signal to handle cleanup
    connect(clientSocket, &QTcpSocket::disconnected,
            this, &RequestHandler::onSocketDisconnected, Qt::UniqueConnection);
    // Connect to destroyed signal to clean up the disconnected socket set
    connect(clientSocket, &QObject::destroyed,
            this, &RequestHandler::onSocketDestroyed, Qt::UniqueConnection);

    PendingRequest pending;
    pending.clientSocket = clientSocket;
    pending.originalRequest = request;  // Save original request for retry
    pending.retryCount = 0;
    pending.maxRetries = m_config->retryCount();
    pending.headersSent = false;
    pending.streamingStarted = false;
    pending.cancelled = false;  // Explicitly initialize
    pending.accumulatedResponse.clear();
    pending.requestTimer.start();

    sendRequest(pending);
}

void RequestHandler::cancelRequestsForSocket(QTcpSocket *socket)
{
    QList<QNetworkReply*> toAbort;

    // First, mark all requests for this socket as cancelled and collect replies
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
            if (it.value().clientSocket == socket) {
                // Mark as cancelled - this stops any further processing
                it.value().cancelled = true;
                toAbort.append(it.key());
            }
        }
        // Remove from map while still holding lock
        for (QNetworkReply *reply : toAbort) {
            m_pendingRequests.remove(reply);
        }
    }
    // Lock released here

    // Now abort and delete replies WITHOUT holding the lock
    // This prevents deadlock when abort() triggers onReplyFinished synchronously
    for (QNetworkReply *reply : toAbort) {
        reply->disconnect(this);  // Disconnect all signals first
        reply->abort();
        reply->deleteLater();
    }

    if (!toAbort.isEmpty()) {
        LOG(QString("RequestHandler: Cancelled %1 pending request(s) for disconnected client").arg(toAbort.size()));
    }
}

void RequestHandler::onSocketDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        // Add to disconnected set FIRST to prevent race conditions
        {
            QMutexLocker locker(&m_mutex);
            m_disconnectedSockets.insert(socket);
        }
        cancelRequestsForSocket(socket);
    }
}

void RequestHandler::onSocketDestroyed(QObject *obj)
{
    // Remove from disconnected set when socket is destroyed
    QTcpSocket *socket = static_cast<QTcpSocket*>(obj);
    if (socket) {
        QMutexLocker locker(&m_mutex);
        m_disconnectedSockets.remove(socket);
    }
}

void RequestHandler::sendRequest(PendingRequest &pending)
{
    // Check if request was cancelled before sending
    if (pending.cancelled) {
        LOG("RequestHandler: Request already cancelled, not sending");
        return;
    }

    if (!isSocketValid(pending.clientSocket)) {
        LOG("RequestHandler: Client disconnected before sending request");
        pending.cancelled = true;
        return;
    }

    // Get atomic snapshot of backend state (prevents race conditions)
    BackendSnapshot backend = m_pool->getBackendSnapshot();

    if (!backend.valid) {
        emit logMessage("No URL or Key available");
        if (isSocketValid(pending.clientSocket)) {
            sendErrorResponse(pending.clientSocket.data(), 503, "No backend available");
        }
        return;
    }

    pending.usedUrlIndex = backend.urlIndex;
    pending.usedKeyIndex = backend.keyIndex;

    // Check if OpenAI format is enabled for current group
    pending.useOpenAIFormat = backend.openAIFormat;
    pending.streamState = Conversion::StreamingState();  // Reset streaming state for each request/retry

    // Check if this is a token count request
    pending.isTokenCountRequest = pending.useOpenAIFormat &&
                                  Conversion::RequestConverter::isTokenCountEndpoint(pending.originalRequest.path);

    // Reset state for retry
    pending.headersSent = false;
    pending.streamingStarted = false;
    pending.accumulatedResponse.clear();

    // Build target URL - ensure proper URL construction
    QString urlString = backend.url;
    QString requestPath = pending.originalRequest.path;

    // Convert endpoint if using OpenAI format
    if (pending.useOpenAIFormat) {
        requestPath = Conversion::RequestConverter::convertEndpoint(requestPath);
        LOG(QString("RequestHandler: OpenAI mode - converted endpoint: %1 -> %2")
                .arg(pending.originalRequest.path, requestPath));
    }

    if (!urlString.endsWith('/') && !requestPath.startsWith('/')) {
        urlString += '/';
    } else if (urlString.endsWith('/') && requestPath.startsWith('/')) {
        urlString.chop(1);
    }
    urlString += requestPath;

    QUrl targetUrl(urlString);
    if (!targetUrl.isValid()) {
        emit logMessage(QString("Error: invalid URL: %1").arg(urlString));
        if (isSocketValid(pending.clientSocket)) {
            sendErrorResponse(pending.clientSocket.data(), 502, "Invalid backend URL");
        }
        return;
    }

    LOG(QString("RequestHandler: Sending to backend (attempt %1/%2): %3")
            .arg(pending.retryCount + 1)
            .arg(pending.maxRetries)
            .arg(targetUrl.toString()));

    emit logMessage(QString("[%1] %2 -> %3 (attempt %4/%5)")
                        .arg(pending.originalRequest.method)
                        .arg(pending.originalRequest.path)
                        .arg(targetUrl.toString())
                        .arg(pending.retryCount + 1)
                        .arg(pending.maxRetries));

    QNetworkRequest netRequest(targetUrl);

    // Configure SSL for better compatibility
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    sslConfig.setPeerVerifyMode(QSslSocket::AutoVerifyPeer);
    netRequest.setSslConfiguration(sslConfig);

    // Set timeouts from config (convert seconds to milliseconds)
    int timeoutMs = m_config->timeoutSeconds() * 1000;
    netRequest.setTransferTimeout(timeoutMs);

    // Copy headers, replacing Authorization and Host
    for (auto it = pending.originalRequest.headers.begin(); it != pending.originalRequest.headers.end(); ++it) {
        QString headerName = it.key().toLower();
        if (headerName == "host") {
            netRequest.setRawHeader("Host", targetUrl.host().toUtf8());
        } else if (headerName == "authorization") {
            // Replace with our key
            continue;
        } else if (headerName == "content-length") {
            // Will be set automatically
            continue;
        } else {
            netRequest.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
    }

    // Set our API key
    netRequest.setRawHeader("Authorization", QString("Bearer %1").arg(backend.key).toUtf8());
    netRequest.setRawHeader("x-api-key", backend.key.toUtf8());

    // Disable ALL compression - request uncompressed responses only
    // This avoids issues with zstd (not supported) and gzip (Qt auto-decompression issues)
    netRequest.setRawHeader("Accept-Encoding", "identity");

    // Ensure Content-Type is set for POST/PUT requests
    if ((pending.originalRequest.method == "POST" || pending.originalRequest.method == "PUT") &&
        !pending.originalRequest.body.isEmpty()) {
        bool hasContentType = false;
        for (auto it = pending.originalRequest.headers.begin(); it != pending.originalRequest.headers.end(); ++it) {
            if (it.key().toLower() == "content-type") {
                hasContentType = true;
                break;
            }
        }
        if (!hasContentType) {
            netRequest.setRawHeader("Content-Type", "application/json");
        }
    }

    // Apply model name mapping to request body
    QByteArray requestBody = pending.originalRequest.body;
    if (!requestBody.isEmpty() && pending.originalRequest.method == "POST") {
        // First, convert to OpenAI format if enabled
        if (pending.useOpenAIFormat) {
            if (pending.isTokenCountRequest) {
                // Token count request - use special converter
                requestBody = Conversion::RequestConverter::convertTokenCountRequest(requestBody, pending.originalModelName);
                emit logMessage(QString("[OpenAI Mode] Converting token count request"));
            } else {
                // Regular request
                requestBody = Conversion::RequestConverter::convert(requestBody, pending.originalModelName);
                emit logMessage(QString("[OpenAI Mode] Converting request format"));
                LOG(QString("RequestHandler: After convert - originalModelName=%1").arg(pending.originalModelName));
            }
            // Apply model mapping on the converted body
            if (!pending.originalModelName.isEmpty()) {
                pending.mappedModelName = m_pool->mapModelName(pending.originalModelName);
                if (pending.mappedModelName != pending.originalModelName) {
                    // Replace model in the OpenAI format body
                    QString bodyStr = QString::fromUtf8(requestBody);
                    bodyStr.replace(QString("\"model\":\"%1\"").arg(pending.originalModelName),
                                   QString("\"model\":\"%1\"").arg(pending.mappedModelName));
                    bodyStr.replace(QString("\"model\": \"%1\"").arg(pending.originalModelName),
                                   QString("\"model\": \"%1\"").arg(pending.mappedModelName));
                    requestBody = bodyStr.toUtf8();
                    LOG(QString("RequestHandler: Model mapping: %1 -> %2").arg(pending.originalModelName, pending.mappedModelName));
                    emit logMessage(QString("Model: %1 -> %2").arg(pending.originalModelName, pending.mappedModelName));
                }
            }
        } else {
            // Standard model mapping without format conversion
            requestBody = replaceModelInRequest(requestBody, pending.originalModelName, pending.mappedModelName);
        }
    }

    // Send request using potentially modified body
    QNetworkReply *reply = nullptr;
    if (pending.originalRequest.method == "GET") {
        reply = m_networkManager->get(netRequest);
    } else if (pending.originalRequest.method == "POST") {
        LOG(QString("RequestHandler: POST body size: %1 bytes").arg(requestBody.size()));
        // Log the request body for debugging tool_calls structure
        QString bodyStr = QString::fromUtf8(requestBody);
        if (bodyStr.contains("tool_calls")) {
            // Find and log the tool_calls section
            int toolCallsPos = bodyStr.indexOf("tool_calls");
            if (toolCallsPos != -1) {
                LOG(QString("RequestHandler: Request body contains tool_calls at position %1").arg(toolCallsPos));
                LOG(QString("RequestHandler: tool_calls context: %1").arg(bodyStr.mid(toolCallsPos, 500)));
            }
        }
        if (m_config->debugLog()) {
            LOG(QString("RequestHandler: FULL request body:\n%1").arg(bodyStr));
        }
        reply = m_networkManager->post(netRequest, requestBody);
    } else if (pending.originalRequest.method == "PUT") {
        reply = m_networkManager->put(netRequest, requestBody);
    } else if (pending.originalRequest.method == "DELETE") {
        reply = m_networkManager->deleteResource(netRequest);
    } else {
        // Custom method
        reply = m_networkManager->sendCustomRequest(netRequest, pending.originalRequest.method.toUtf8(), requestBody);
    }

    if (reply) {
        {
            QMutexLocker locker(&m_mutex);
            m_pendingRequests[reply] = pending;
        }
        connect(reply, &QNetworkReply::finished, this, &RequestHandler::onReplyFinished);
        connect(reply, &QNetworkReply::readyRead, this, &RequestHandler::onReplyReadyRead);
        connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
                this, &RequestHandler::onReplyError);

        // Track download progress for debugging
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            LOG(QString("RequestHandler: downloadProgress received=%1, total=%2").arg(received).arg(total));
        });
    } else {
        emit logMessage("Error: failed to create network request");
        if (isSocketValid(pending.clientSocket)) {
            sendErrorResponse(pending.clientSocket.data(), 500, "Failed to create request");
        }
    }
}

void RequestHandler::onReplyReadyRead()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    qint64 bytesAvailable = reply->bytesAvailable();
    LOG(QString("RequestHandler: onReplyReadyRead called, bytesAvailable=%1").arg(bytesAvailable));

    // Read data first (outside lock to avoid blocking)
    QByteArray data = reply->readAll();
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Now lock and process
    QMutexLocker locker(&m_mutex);
    if (!m_pendingRequests.contains(reply)) {
        LOG("RequestHandler: Reply not in pending requests in onReplyReadyRead");
        return;
    }

    PendingRequest &pending = m_pendingRequests[reply];

    // Check if request was cancelled (client disconnected)
    if (pending.cancelled) {
        LOG("RequestHandler: Request cancelled, ignoring data");
        return;
    }

    if (!isSocketValidLocked(pending.clientSocket)) {
        LOG("RequestHandler: Socket invalid in onReplyReadyRead, marking cancelled");
        pending.cancelled = true;
        return;
    }

    // Check for zstd compression on first data chunk
    if (!pending.headersSent && !pending.needsZstdDecompression) {
        pending.needsZstdDecompression = isZstdCompressed(reply);
        if (pending.needsZstdDecompression) {
            LOG("RequestHandler: Response is zstd compressed, will buffer and decompress");
        }
    }

    // If zstd compressed, buffer all data and wait for onReplyFinished to decompress
    if (pending.needsZstdDecompression) {
        pending.zstdBuffer.append(data);
        pending.accumulatedResponse.append(data);  // Also accumulate for error detection
        LOG(QString("RequestHandler: Buffering zstd data, total buffered: %1 bytes").arg(pending.zstdBuffer.size()));
        return;  // Don't process until we have all data
    }

    QTcpSocket *socket = pending.clientSocket.data();

    // Accumulate response for null detection (before streaming starts)
    if (!pending.streamingStarted) {
        pending.accumulatedResponse.append(data);

        // Check if we need to retry due to error status - wait for full response
        if (statusCode >= 400 || statusCode == 0) {
            LOG(QString("RequestHandler: Error status %1, waiting for full response").arg(statusCode));
            return;
        }

        // Check accumulated data for null response (can retry if not streaming yet)
        if (isNullResponse(pending.accumulatedResponse)) {
            LOG("RequestHandler: Detected null response in streaming data, will check for retry in onReplyFinished");
            // Don't start streaming, wait for onReplyFinished to handle retry
            return;
        }
    }

    // For streaming responses, send headers first
    if (!pending.headersSent) {
        // Log all response headers for debugging
        QList<QByteArray> headerList = reply->rawHeaderList();
        for (const QByteArray &header : headerList) {
            LOG(QString("RequestHandler: Response header: %1: %2")
                    .arg(QString::fromUtf8(header))
                    .arg(QString::fromUtf8(reply->rawHeader(header))));
        }

        LOG(QString("RequestHandler: Sending headers, status=%1").arg(statusCode));
        pending.headersSent = true;

        // Copy necessary data before unlocking
        bool useOpenAI = pending.useOpenAIFormat;
        QString origModel = pending.originalModelName;
        QString mappedModel = pending.mappedModelName;
        Conversion::StreamingState streamStateCopy = pending.streamState;

        locker.unlock();
        sendResponseHeaders(socket, reply);
        locker.relock();

        // Re-check after unlock - pending might have been removed or cancelled
        if (!m_pendingRequests.contains(reply)) {
            LOG("RequestHandler: Reply removed during header send");
            return;
        }
        PendingRequest &pendingAfter = m_pendingRequests[reply];
        if (pendingAfter.cancelled) {
            LOG("RequestHandler: Request cancelled during header send");
            return;
        }
        if (!isSocketValidLocked(pendingAfter.clientSocket)) {
            LOG("RequestHandler: Socket invalid after header send");
            pendingAfter.cancelled = true;
            return;
        }
        socket = pendingAfter.clientSocket.data();
    }

    // Mark streaming as started - after this point we cannot retry
    if (!pending.streamingStarted && !data.isEmpty()) {
        pending.streamingStarted = true;
        LOG("RequestHandler: Streaming started - retry no longer possible for this request");
    }

    // Copy data needed for processing outside lock
    bool useOpenAIFormat = pending.useOpenAIFormat;
    QString originalModelName = pending.originalModelName;
    QString mappedModelName = pending.mappedModelName;
    // For streaming state, we need to update it after processing
    Conversion::StreamingState streamStateCopy = pending.streamState;

    locker.unlock();

    // Log streaming content for debugging
    LOG(QString("RequestHandler: Read %1 bytes of body data").arg(data.size()));
    if (!data.isEmpty()) {
        QString dataPreview = QString::fromUtf8(data);
        if (dataPreview.length() > 500) {
            LOG(QString("RequestHandler: Streaming data (truncated): %1...").arg(dataPreview.left(500)));
        } else {
            LOG(QString("RequestHandler: Streaming data: %1").arg(dataPreview));
        }
    }

    // Apply reverse model mapping to response data (or OpenAI format conversion)
    if (useOpenAIFormat) {
        // Convert OpenAI streaming format to Claude format
        LOG(QString("RequestHandler: Converting streaming chunk with originalModel=%1, mappedModel=%2")
                .arg(originalModelName, mappedModelName));
        data = Conversion::StreamingConverter::convertChunk(data, originalModelName, streamStateCopy);

        // Update stream state back to pending request
        locker.relock();
        if (m_pendingRequests.contains(reply) && !m_pendingRequests[reply].cancelled) {
            m_pendingRequests[reply].streamState = streamStateCopy;
        }
        locker.unlock();
    } else if (!originalModelName.isEmpty() && !mappedModelName.isEmpty()) {
        data = replaceModelInResponse(data, mappedModelName, originalModelName);
    }

    // Write data to client - use QPointer for safety
    QPointer<QTcpSocket> socketPtr = socket;
    if (!data.isEmpty() && socketPtr && socketPtr->state() == QAbstractSocket::ConnectedState) {
        qint64 written = socketPtr->write(data);
        socketPtr->flush();  // Ensure data is sent immediately
        LOG(QString("RequestHandler: Wrote %1 bytes to client").arg(written));
    }
}

void RequestHandler::onReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    LOG("RequestHandler: onReplyFinished called");
    LOG("---------------------------------------------");

    PendingRequest pending;
    bool wasCancelled = false;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_pendingRequests.contains(reply)) {
            LOG("RequestHandler: Reply not in pending requests (already cancelled or removed)");
            LOG("---------------------------------------------");
            reply->deleteLater();
            return;
        }
        pending = m_pendingRequests.take(reply);
        wasCancelled = pending.cancelled;
    }

    // If cancelled (client disconnected), just cleanup and return
    if (wasCancelled) {
        LOG("RequestHandler: Request was cancelled (client disconnected), cleaning up");
        LOG("---------------------------------------------");
        reply->deleteLater();
        return;
    }

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray remainingBody = reply->readAll();
    pending.accumulatedResponse.append(remainingBody);

    // Handle zstd decompression if needed
    if (pending.needsZstdDecompression) {
        pending.zstdBuffer.append(remainingBody);
        LOG(QString("RequestHandler: Decompressing zstd data, compressed size: %1 bytes").arg(pending.zstdBuffer.size()));
        QByteArray decompressed = decompressZstd(pending.zstdBuffer);
        if (!decompressed.isEmpty()) {
            pending.accumulatedResponse = decompressed;
            LOG(QString("RequestHandler: zstd decompression successful, decompressed size: %1 bytes").arg(decompressed.size()));
        } else {
            // zstd decompression failed - this is an error condition
            LOG("RequestHandler: zstd decompression failed");
#ifndef HAVE_ZSTD
            // zstd not compiled in - return error to client
            LOG("RequestHandler: zstd support not available, returning error");
            if (isSocketValid(pending.clientSocket)) {
                sendErrorResponse(pending.clientSocket.data(), 502,
                    "Server returned zstd compressed response but zstd decompression is not available. "
                    "Please rebuild with zstd support or use a different backend.");
            }
            reply->deleteLater();
            emit requestError();
            return;
#endif
        }
    }

    bool hasNetworkError = reply->error() != QNetworkReply::NoError;
    QString errorString = reply->errorString();

    qint64 elapsed = pending.requestTimer.elapsed();

    // Detailed response logging
    LOG(QString("RequestHandler: Response Status: %1").arg(statusCode));
    LOG(QString("RequestHandler: Network Error: %1").arg(hasNetworkError ? errorString : "none"));
    LOG(QString("RequestHandler: Total Response Size: %1 bytes").arg(pending.accumulatedResponse.size()));
    LOG(QString("RequestHandler: Request Duration: %1 ms").arg(elapsed));
    LOG(QString("RequestHandler: Streaming Started: %1").arg(pending.streamingStarted ? "yes" : "no"));

    // Log response body content (truncate if too large)
    if (!pending.accumulatedResponse.isEmpty()) {
        QString bodyPreview = QString::fromUtf8(pending.accumulatedResponse);
        if (bodyPreview.length() > 2000) {
            LOG(QString("RequestHandler: Body Content (truncated):\n%1\n... [truncated, total %2 bytes]")
                    .arg(bodyPreview.left(2000))
                    .arg(pending.accumulatedResponse.size()));
        } else {
            LOG(QString("RequestHandler: Body Content:\n%1").arg(bodyPreview));
        }
    } else {
        LOG("RequestHandler: Body Content: <empty>");
    }

    // Check socket validity before proceeding
    if (!isSocketValid(pending.clientSocket)) {
        LOG("RequestHandler: Client disconnected before response could be sent");
        LOG("---------------------------------------------");
        reply->deleteLater();
        return;
    }

    // Check for null/empty response
    // IMPORTANT: Only check for null response if streaming hasn't started yet
    bool nullResponseDetected = false;
    if (!pending.streamingStarted) {
        nullResponseDetected = isNullResponse(pending.accumulatedResponse);
    }
    bool correctionEnabled = m_config->correctionEnabled();

    if (nullResponseDetected && statusCode == 200) {
        if (correctionEnabled) {
            LOG("RequestHandler: WARNING - Detected empty/null response with 200 status! Correction enabled, will retry.");
        } else {
            LOG("RequestHandler: WARNING - Detected empty/null response with 200 status! Correction disabled, not retrying.");
        }
    }

    LOG("---------------------------------------------");

    // Determine if we should retry
    bool isError = hasNetworkError || statusCode == 0 || statusCode >= 400;
    bool canRetry = !pending.streamingStarted && pending.retryCount < pending.maxRetries - 1;
    bool shouldRetry = canRetry && (isError || (statusCode == 200 && nullResponseDetected && correctionEnabled));

    if (shouldRetry) {
        // Double-check socket is still valid before retrying
        if (!isSocketValid(pending.clientSocket)) {
            LOG("RequestHandler: Client disconnected, aborting retry");
            reply->deleteLater();
            return;
        }

        QString retryReason;
        if (nullResponseDetected && statusCode == 200) {
            if (pending.accumulatedResponse.isEmpty()) {
                retryReason = "empty response (correction)";
            } else {
                retryReason = "null response (correction)";
            }
        } else if (hasNetworkError) {
            retryReason = QString("network error: %1").arg(errorString);
        } else {
            retryReason = QString("status %1").arg(statusCode);
        }

        LOG(QString("RequestHandler: Retry %1/%2 (reason: %3)")
                .arg(pending.retryCount + 2)
                .arg(pending.maxRetries)
                .arg(retryReason));

        emit logMessage(QString("Retry %1/%2 (%3)")
                            .arg(pending.retryCount + 2)
                            .arg(pending.maxRetries)
                            .arg(retryReason));

        // Emit correction signal if this is a null response retry
        if (nullResponseDetected && statusCode == 200) {
            emit correctionTriggered();
        }

        // Determine how to switch backend based on error type
        bool switched = false;

        if (nullResponseDetected) {
            // Null response - try switching URL first
            switched = m_pool->switchToNextUrl();
            if (!switched) {
                switched = m_pool->switchToNextKey();
            }
            if (!switched) {
                emit logMessage("Null response retry - no other backend available, retrying same");
            }
        } else if (hasNetworkError || isServerError(statusCode)) {
            // Network error or 5xx/520 - switch URL
            m_pool->markUrlUnavailable(pending.usedUrlIndex, m_config->cooldownSeconds());
            switched = m_pool->switchToNextUrl();
            if (!switched) {
                m_pool->markUrlAvailable(pending.usedUrlIndex);
                emit logMessage("No other URL available, retrying with same URL");
            }
        } else if (isRateLimitError(statusCode, pending.accumulatedResponse)) {
            // Rate limit - switch Key with cooldown
            m_pool->markKeyUnavailable(pending.usedKeyIndex, m_config->cooldownSeconds());
            switched = m_pool->switchToNextKey();
            if (!switched) {
                m_pool->markKeyAvailable(pending.usedKeyIndex);
                emit logMessage("No other Key available, retrying with same Key");
            }
        } else if (isQuotaExhaustedError(statusCode, pending.accumulatedResponse)) {
            // Quota exhausted - switch Key permanently
            m_pool->markKeyUnavailable(pending.usedKeyIndex, 0);
            switched = m_pool->switchToNextKey();
            if (!switched) {
                emit logMessage("All API keys exhausted!");
            }
        } else {
            // Other 4xx errors
            switched = m_pool->switchToNextUrl();
            if (!switched) {
                switched = m_pool->switchToNextKey();
            }
        }

        // Final socket check before retry
        if (isSocketValid(pending.clientSocket)) {
            pending.retryCount++;
            pending.cancelled = false;  // Reset cancelled flag for retry
            reply->deleteLater();
            sendRequest(pending);
            return;
        } else {
            LOG("RequestHandler: Client disconnected during retry preparation");
            reply->deleteLater();
            return;
        }
    }

    // No retry - send response to client
    if (isError || nullResponseDetected) {
        if (pending.streamingStarted) {
            // This is informational - streaming already sent data, can't retry
            // Only log to file, not UI (reduce noise)
            LOG(QString("RequestHandler: Backend error after streaming started (status=%1) - response already sent to client").arg(statusCode));
        } else {
            emit logMessage(QString("Max retries (%1) exceeded, returning error to client").arg(pending.maxRetries));
        }
        emit requestError();
    } else {
        emit requestSuccess();
    }

    if (isSocketValid(pending.clientSocket)) {
        if (!pending.headersSent) {
            if (hasNetworkError && statusCode == 0) {
                // Network error without HTTP response
                sendErrorResponse(pending.clientSocket.data(), 502, errorString);
                reply->deleteLater();
                return;
            }
            // For zstd responses, send modified headers (without content-encoding)
            if (pending.needsZstdDecompression) {
                sendResponseHeadersWithoutEncoding(pending.clientSocket.data(), reply);
            } else {
                sendResponseHeaders(pending.clientSocket.data(), reply);
            }
        }

        // Apply reverse model mapping to remaining body (or OpenAI format conversion)
        QByteArray bodyToSend;

        // For zstd decompressed responses, use the full decompressed data
        if (pending.needsZstdDecompression) {
            bodyToSend = pending.accumulatedResponse;
        } else {
            bodyToSend = remainingBody;
        }

        if (pending.useOpenAIFormat && !pending.streamingStarted) {
            if (pending.isTokenCountRequest) {
                // Token count response - use special converter
                bodyToSend = Conversion::ResponseConverter::convertTokenCountResponse(pending.accumulatedResponse);
                LOG("RequestHandler: Converted OpenAI token count response to Claude format");
            } else {
                // Non-streaming response - convert full response
                bodyToSend = Conversion::ResponseConverter::convert(pending.accumulatedResponse, pending.originalModelName);
                LOG("RequestHandler: Converted OpenAI response to Claude format");
            }
        } else if (!pending.originalModelName.isEmpty() && !pending.mappedModelName.isEmpty() && !pending.needsZstdDecompression) {
            bodyToSend = replaceModelInResponse(remainingBody, pending.mappedModelName, pending.originalModelName);
        }

        // Send remaining body data
        if (!bodyToSend.isEmpty()) {
            pending.clientSocket->write(bodyToSend);
        }
        pending.clientSocket->flush();
        emit requestCompleted(pending.clientSocket.data());
    }

    reply->deleteLater();
}

void RequestHandler::onReplyError(QNetworkReply::NetworkError error)
{
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    QString errorDetails;
    switch (error) {
    case QNetworkReply::ConnectionRefusedError:
        errorDetails = "Connection refused";
        break;
    case QNetworkReply::RemoteHostClosedError:
        errorDetails = "Remote host closed connection";
        break;
    case QNetworkReply::HostNotFoundError:
        errorDetails = "Host not found";
        break;
    case QNetworkReply::TimeoutError:
        errorDetails = "Connection timeout";
        break;
    case QNetworkReply::SslHandshakeFailedError:
        errorDetails = "SSL handshake failed";
        break;
    case QNetworkReply::ProtocolFailure:
        errorDetails = "Protocol failure (malformed response)";
        break;
    case QNetworkReply::OperationCanceledError:
        LOG(QString("RequestHandler: Operation canceled"));
        return;
    default:
        errorDetails = QString("Error code %1").arg(error);
    }

    LOG(QString("RequestHandler: Network error: %1 - %2").arg(errorDetails, reply->errorString()));
    if (error != QNetworkReply::RemoteHostClosedError) {
        emit logMessage(QString("Network error: %1").arg(errorDetails));
    }
}

void RequestHandler::handleErrorResponse(QNetworkReply *reply, PendingRequest &pending)
{
    Q_UNUSED(reply)
    Q_UNUSED(pending)
}

bool RequestHandler::shouldRetryOnError(int statusCode, const QByteArray &body)
{
    Q_UNUSED(body)
    return statusCode == 0 || statusCode >= 400;
}

bool RequestHandler::isRateLimitError(int statusCode, const QByteArray &body)
{
    if (statusCode == 429) return true;

    QString bodyStr = QString::fromUtf8(body).toLower();
    return bodyStr.contains("rate_limit") || bodyStr.contains("rate limit") ||
           bodyStr.contains("too many requests");
}

bool RequestHandler::isQuotaExhaustedError(int statusCode, const QByteArray &body)
{
    if (statusCode == 402) return true;

    if (statusCode == 403) {
        QString bodyStr = QString::fromUtf8(body).toLower();
        return bodyStr.contains("insufficient") ||
               bodyStr.contains("quota") ||
               bodyStr.contains("credit") ||
               bodyStr.contains("billing") ||
               bodyStr.contains("exceeded");
    }
    return false;
}

bool RequestHandler::isServerError(int statusCode)
{
    return statusCode == 520 || statusCode == 521 || statusCode == 522 ||
           statusCode == 523 || statusCode == 524 || (statusCode >= 500 && statusCode < 600);
}

bool RequestHandler::isNullResponse(const QByteArray &body)
{
    // Empty body is considered a null/invalid response
    if (body.isEmpty()) return true;

    QString bodyStr = QString::fromUtf8(body).trimmed();

    // Check various null patterns that cause Claude Code errors
    if (bodyStr == "null") return true;
    if (bodyStr.startsWith("data: null")) return true;

    // Check for SSE format with null
    if (bodyStr.contains("\ndata: null\n")) return true;
    if (bodyStr.contains("\r\ndata: null\r\n")) return true;

    // IMPORTANT: Many null fields are NORMAL in OpenAI responses:
    // - "content":null in tool_calls responses
    // - "type":null in streaming tool_calls delta (after first chunk)
    // - "usage":null, "logprobs":null, "system_fingerprint":null, etc.
    // Only flag as null if it's a truly empty/invalid response

    // Check if this looks like a valid OpenAI response (has choices or error)
    bool hasChoices = bodyStr.contains("\"choices\"");
    bool hasError = bodyStr.contains("\"error\"");
    bool hasToolCalls = bodyStr.contains("tool_calls") || bodyStr.contains("tool_use");
    bool hasDelta = bodyStr.contains("\"delta\"");
    bool hasMessage = bodyStr.contains("\"message\"");

    // If it has choices with tool_calls, delta, or message, it's likely valid
    if (hasChoices && (hasToolCalls || hasDelta || hasMessage)) {
        return false;
    }

    // If it has an error object, let it through (error handling elsewhere)
    if (hasError) {
        return false;
    }

    // For non-tool_calls responses without choices, check for problematic null patterns
    if (!hasChoices && !hasToolCalls) {
        // Check for JSON with null content (common Claude API error)
        if (bodyStr.contains("\"content\":null") ||
            bodyStr.contains("\"content\": null") ||
            bodyStr.contains("\"value\":null") ||
            bodyStr.contains("\"value\": null") ||
            bodyStr.contains("\"text\":null") ||
            bodyStr.contains("\"text\": null")) {
            return true;
        }
    }

    return false;
}

void RequestHandler::sendErrorResponse(QTcpSocket *socket, int statusCode, const QString &message)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;

    QString escapedMessage = message;
    escapedMessage.replace("\\", "\\\\");
    escapedMessage.replace("\"", "\\\"");
    escapedMessage.replace("\n", "\\n");
    escapedMessage.replace("\r", "\\r");

    QByteArray body = QString("{\"error\": {\"message\": \"%1\", \"type\": \"proxy_error\"}}").arg(escapedMessage).toUtf8();
    QString response = QString("HTTP/1.1 %1 Error\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %2\r\n"
                               "Connection: close\r\n"
                               "\r\n").arg(statusCode).arg(body.size());

    socket->write(response.toUtf8());
    socket->write(body);
    socket->flush();
    emit requestCompleted(socket);
}

void RequestHandler::sendResponseHeaders(QTcpSocket *socket, QNetworkReply *reply)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    if (!reply) return;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    if (statusText.isEmpty()) {
        statusText = "OK";
    }

    QString response = QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText);

    QList<QByteArray> headerList = reply->rawHeaderList();
    for (const QByteArray &header : headerList) {
        QString headerName = QString::fromUtf8(header).toLower();
        if (headerName == "transfer-encoding" || headerName == "connection") {
            continue;
        }
        response += QString("%1: %2\r\n").arg(QString::fromUtf8(header), QString::fromUtf8(reply->rawHeader(header)));
    }

    response += "Connection: close\r\n";
    response += "\r\n";

    socket->write(response.toUtf8());
}

void RequestHandler::sendResponseHeadersWithoutEncoding(QTcpSocket *socket, QNetworkReply *reply)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    if (!reply) return;

    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    if (statusText.isEmpty()) {
        statusText = "OK";
    }

    QString response = QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText);

    QList<QByteArray> headerList = reply->rawHeaderList();
    for (const QByteArray &header : headerList) {
        QString headerName = QString::fromUtf8(header).toLower();
        // Skip encoding-related headers since we've decompressed the content
        if (headerName == "transfer-encoding" || headerName == "connection" ||
            headerName == "content-encoding" || headerName == "content-length") {
            continue;
        }
        response += QString("%1: %2\r\n").arg(QString::fromUtf8(header), QString::fromUtf8(reply->rawHeader(header)));
    }

    response += "Connection: close\r\n";
    response += "\r\n";

    socket->write(response.toUtf8());
}

QByteArray RequestHandler::replaceModelInRequest(const QByteArray &body, QString &originalModel, QString &mappedModel)
{
    if (body.isEmpty()) return body;

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return body;  // Not valid JSON, return as-is
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("model")) {
        return body;  // No model field
    }

    originalModel = obj["model"].toString();
    if (originalModel.isEmpty()) {
        return body;
    }

    // Get mapped model name from pool
    mappedModel = m_pool->mapModelName(originalModel);

    if (mappedModel == originalModel) {
        return body;  // No mapping needed
    }

    // Replace model name
    obj["model"] = mappedModel;
    QJsonDocument newDoc(obj);

    LOG(QString("RequestHandler: Model mapping: %1 -> %2").arg(originalModel, mappedModel));
    emit logMessage(QString("Model: %1 -> %2").arg(originalModel, mappedModel));

    return newDoc.toJson(QJsonDocument::Compact);
}

QByteArray RequestHandler::replaceModelInResponse(const QByteArray &body, const QString &mappedModel, const QString &originalModel)
{
    if (body.isEmpty() || mappedModel.isEmpty() || originalModel.isEmpty()) {
        return body;
    }

    if (mappedModel == originalModel) {
        return body;  // No mapping was done
    }

    // For SSE (streaming) responses, we need to handle line by line
    // For JSON responses, we can parse and replace

    QString bodyStr = QString::fromUtf8(body);

    // Simple string replacement - works for both JSON and SSE
    // Replace "model":"mapped" with "model":"original"
    QString searchPattern1 = QString("\"model\":\"%1\"").arg(mappedModel);
    QString replacePattern1 = QString("\"model\":\"%1\"").arg(originalModel);
    bodyStr.replace(searchPattern1, replacePattern1);

    // Also handle with spaces
    QString searchPattern2 = QString("\"model\": \"%1\"").arg(mappedModel);
    QString replacePattern2 = QString("\"model\": \"%1\"").arg(originalModel);
    bodyStr.replace(searchPattern2, replacePattern2);

    return bodyStr.toUtf8();
}

bool RequestHandler::isZstdCompressed(QNetworkReply *reply)
{
    if (!reply) return false;
    QString encoding = QString::fromUtf8(reply->rawHeader("Content-Encoding")).toLower();
    return encoding.contains("zstd");
}

QByteArray RequestHandler::decompressZstd(const QByteArray &compressed)
{
#ifdef HAVE_ZSTD
    if (compressed.isEmpty()) {
        return compressed;
    }

    // Get decompressed size
    unsigned long long decompressedSize = ZSTD_getFrameContentSize(compressed.constData(), compressed.size());

    if (decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        LOG("RequestHandler: zstd - not a valid zstd frame");
        return QByteArray();
    }

    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Size unknown, use streaming decompression with growing buffer
        LOG("RequestHandler: zstd - content size unknown, using streaming decompression");
        decompressedSize = compressed.size() * 10;  // Initial estimate
    }

    // Allocate output buffer
    QByteArray decompressed;
    decompressed.resize(static_cast<int>(decompressedSize));

    // Decompress
    size_t result = ZSTD_decompress(decompressed.data(), decompressed.size(),
                                     compressed.constData(), compressed.size());

    if (ZSTD_isError(result)) {
        LOG(QString("RequestHandler: zstd decompression error: %1").arg(ZSTD_getErrorName(result)));
        return QByteArray();
    }

    decompressed.resize(static_cast<int>(result));
    LOG(QString("RequestHandler: zstd decompressed %1 bytes -> %2 bytes")
            .arg(compressed.size()).arg(decompressed.size()));

    return decompressed;
#else
    LOG("RequestHandler: zstd support not compiled in, cannot decompress");
    return QByteArray();
#endif
}
