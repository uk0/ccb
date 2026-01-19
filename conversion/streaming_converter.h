#ifndef CONVERSION_STREAMING_CONVERTER_H
#define CONVERSION_STREAMING_CONVERTER_H

#include "types.h"
#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

namespace Conversion {

/**
 * StreamingConverter - Converts OpenAI streaming responses to Claude SSE format.
 *
 * Following the exact pattern from copilot-api stream-translation.ts
 *
 * Usage:
 *   StreamingState state;
 *   while (hasChunks) {
 *       QByteArray claudeSSE = StreamingConverter::convertChunk(chunk, model, state);
 *       sendToClient(claudeSSE);
 *   }
 */
class StreamingConverter {
public:
    /**
     * Convert an OpenAI streaming chunk to Claude SSE format.
     *
     * @param chunk The OpenAI SSE data (may contain multiple data: lines)
     * @param originalModel The original Claude model name
     * @param state Per-request streaming state (modified in place)
     * @return Claude SSE formatted data
     */
    static QByteArray convertChunk(const QByteArray &chunk, const QString &originalModel,
                                   StreamingState &state);

    /**
     * Generate an error event.
     */
    static QJsonObject createErrorEvent(const QString &errorType, const QString &message);

private:
    /**
     * Main translation function - converts one OpenAI chunk to Anthropic events.
     * Follows stream-translation.ts translateChunkToAnthropicEvents
     */
    static QJsonArray translateChunkToAnthropicEvents(const QJsonObject &chunk,
                                                       const QString &originalModel,
                                                       StreamingState &state);

    /**
     * Map OpenAI finish_reason to Anthropic stop_reason.
     */
    static QString mapStopReason(const QString &finishReason);

    /**
     * Generate a unique message ID.
     */
    static QString generateMessageId();

    /**
     * Format an SSE event.
     */
    static QByteArray formatSSE(const QString &eventType, const QJsonObject &data);

    // Event creation methods
    static QJsonObject createMessageStartEvent(const QString &chunkId, const QString &model,
                                                const StreamingState &state);
    static QJsonObject createTextBlockStartEvent(int index);
    static QJsonObject createToolBlockStartEvent(int index, const QString &toolId, const QString &name);
    static QJsonObject createThinkingBlockStartEvent(int index);
    static QJsonObject createTextDeltaEvent(int index, const QString &text);
    static QJsonObject createThinkingDeltaEvent(int index, const QString &thinking);
    static QJsonObject createInputJsonDeltaEvent(int index, const QString &partialJson);
    static QJsonObject createBlockStopEvent(int index);
    static QJsonObject createMessageDeltaEvent(const QString &stopReason, const StreamingState &state);
    static QJsonObject createMessageStopEvent();
};

} // namespace Conversion

#endif // CONVERSION_STREAMING_CONVERTER_H
