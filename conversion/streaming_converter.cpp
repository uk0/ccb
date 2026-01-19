#include "streaming_converter.h"
#include "constants.h"
#include "../logger.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace Conversion {

// Following the exact pattern from copilot-api stream-translation.ts

QByteArray StreamingConverter::convertChunk(const QByteArray &chunk, const QString &originalModel,
                                            StreamingState &state)
{
    QString chunkStr = QString::fromUtf8(chunk).trimmed();
    QStringList lines = chunkStr.split('\n', Qt::SkipEmptyParts);
    QByteArray result;

    for (const QString &line : lines) {
        if (!line.startsWith("data: ")) {
            continue;
        }

        QString jsonStr = line.mid(6).trimmed();

        // Handle [DONE] signal
        if (jsonStr == "[DONE]") {
            // Don't send anything for [DONE] - the finish_reason handles closing
            continue;
        }

        // Parse JSON
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            LOG(QString("StreamingConverter: JSON parse error: %1").arg(error.errorString()));
            continue;
        }

        QJsonObject chunkObj = doc.object();
        QJsonArray events = translateChunkToAnthropicEvents(chunkObj, originalModel, state);

        // Format each event as SSE
        for (const QJsonValue &eventVal : events) {
            QJsonObject event = eventVal.toObject();
            QString eventType = event.value("type").toString();
            result += formatSSE(eventType, event);
        }
    }

    return result;
}

// Main translation function - follows stream-translation.ts translateChunkToAnthropicEvents
QJsonArray StreamingConverter::translateChunkToAnthropicEvents(const QJsonObject &chunk,
                                                                const QString &originalModel,
                                                                StreamingState &state)
{
    QJsonArray events;

    // Extract usage FIRST - before checking choices
    // OpenAI may send final chunk with empty choices but with usage data
    // (when stream_options.include_usage = true)
    if (chunk.contains("usage")) {
        QJsonObject usage = chunk.value("usage").toObject();
        state.inputTokens = usage.value("prompt_tokens").toInt(0);
        state.outputTokens = usage.value("completion_tokens").toInt(0);

        QJsonObject promptDetails = usage.value("prompt_tokens_details").toObject();
        if (promptDetails.contains("cached_tokens")) {
            state.cacheReadTokens = promptDetails.value("cached_tokens").toInt(0);
        }
    }

    QJsonArray choices = chunk.value("choices").toArray();
    if (choices.isEmpty()) {
        return events;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice.value("delta").toObject();
    QString finishReason = choice.value("finish_reason").toString();

    // Send message_start if not sent yet
    if (!state.messageStartSent) {
        state.messageId = generateMessageId();
        LOG(QString("StreamingConverter: Creating message_start with model=%1").arg(originalModel));
        events.append(createMessageStartEvent(chunk.value("id").toString(), originalModel, state));
        state.messageStartSent = true;
    }

    // Handle reasoning_content (thinking) from DeepSeek R1 and similar models
    if (delta.contains("reasoning_content") && !delta.value("reasoning_content").isNull()) {
        QString reasoningContent = delta.value("reasoning_content").toString();
        if (!reasoningContent.isEmpty()) {
            // Close any non-thinking block that's open
            if (state.contentBlockOpen && !state.isThinkingBlockOpen()) {
                events.append(createBlockStopEvent(state.contentBlockIndex));
                state.contentBlockIndex++;
                state.contentBlockOpen = false;
            }

            // Start thinking block if not open
            if (!state.thinkingBlockOpen) {
                LOG(QString("StreamingConverter: Starting thinking block at index %1").arg(state.contentBlockIndex));
                state.thinkingBlockIndex = state.contentBlockIndex;
                events.append(createThinkingBlockStartEvent(state.contentBlockIndex));
                state.thinkingBlockOpen = true;
                state.contentBlockOpen = true;
            }

            // Send thinking delta
            events.append(createThinkingDeltaEvent(state.thinkingBlockIndex, reasoningContent));
        }
    }

    // Handle text content delta
    if (delta.contains("content") && !delta.value("content").isNull()) {
        QString content = delta.value("content").toString();
        if (!content.isEmpty()) {
            // If a thinking block was open, close it first
            if (state.isThinkingBlockOpen()) {
                LOG(QString("StreamingConverter: Closing thinking block at index %1").arg(state.thinkingBlockIndex));
                events.append(createBlockStopEvent(state.thinkingBlockIndex));
                state.contentBlockIndex++;
                state.thinkingBlockOpen = false;
                state.thinkingBlockIndex = -1;
                state.contentBlockOpen = false;
            }

            // If a tool block was open, close it first
            if (state.isToolBlockOpen()) {
                events.append(createBlockStopEvent(state.contentBlockIndex));
                state.contentBlockIndex++;
                state.contentBlockOpen = false;
            }

            // Start text block if not open
            if (!state.contentBlockOpen) {
                events.append(createTextBlockStartEvent(state.contentBlockIndex));
                state.contentBlockOpen = true;
            }

            // Send text delta
            events.append(createTextDeltaEvent(state.contentBlockIndex, content));
        }
    }

    // Handle tool calls
    if (delta.contains("tool_calls")) {
        QJsonArray toolCalls = delta.value("tool_calls").toArray();

        for (const QJsonValue &tcVal : toolCalls) {
            QJsonObject tc = tcVal.toObject();
            int openAIIndex = tc.value("index").toInt();

            // Check if this is a new tool call (has id and function.name)
            if (tc.contains("id") && tc.value("function").toObject().contains("name")) {
                QString toolId = tc.value("id").toString();
                QString toolName = tc.value("function").toObject().value("name").toString();

                // Close any previously open block
                if (state.contentBlockOpen) {
                    events.append(createBlockStopEvent(state.contentBlockIndex));
                    state.contentBlockIndex++;
                    state.contentBlockOpen = false;
                }

                // Store tool call info
                int anthropicBlockIndex = state.contentBlockIndex;
                StreamingState::ToolCallInfo info;
                info.id = toolId;
                info.name = toolName;
                info.anthropicBlockIndex = anthropicBlockIndex;
                state.toolCalls[openAIIndex] = info;

                // Send tool_use block start
                events.append(createToolBlockStartEvent(anthropicBlockIndex, toolId, toolName));
                state.contentBlockOpen = true;
            }

            // Handle arguments delta
            if (tc.value("function").toObject().contains("arguments")) {
                QString args = tc.value("function").toObject().value("arguments").toString();
                if (!args.isEmpty() && state.toolCalls.contains(openAIIndex)) {
                    int blockIndex = state.toolCalls[openAIIndex].anthropicBlockIndex;
                    events.append(createInputJsonDeltaEvent(blockIndex, args));
                }
            }
        }
    }

    // Handle finish reason
    if (!finishReason.isEmpty() && finishReason != "null") {
        // Close any open thinking block
        if (state.isThinkingBlockOpen()) {
            LOG(QString("StreamingConverter: Closing thinking block on finish at index %1").arg(state.thinkingBlockIndex));
            events.append(createBlockStopEvent(state.thinkingBlockIndex));
            state.thinkingBlockOpen = false;
            state.thinkingBlockIndex = -1;
            state.contentBlockOpen = false;
        }

        // Close any other open block
        if (state.contentBlockOpen) {
            events.append(createBlockStopEvent(state.contentBlockIndex));
            state.contentBlockOpen = false;
        }

        // Send message_delta with stop_reason
        QString stopReason = mapStopReason(finishReason);
        events.append(createMessageDeltaEvent(stopReason, state));

        // Send message_stop
        events.append(createMessageStopEvent());
    }

    return events;
}

QString StreamingConverter::mapStopReason(const QString &finishReason)
{
    // Exact mapping from copilot-api utils.ts
    if (finishReason == "stop") {
        return QString(STOP_END_TURN);
    } else if (finishReason == "length") {
        return QString(STOP_MAX_TOKENS);
    } else if (finishReason == "tool_calls") {
        return QString(STOP_TOOL_USE);
    } else if (finishReason == "content_filter") {
        return QString(STOP_END_TURN);
    }
    return QString(STOP_END_TURN);
}

QString StreamingConverter::generateMessageId()
{
    return QString("msg_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(24));
}

QByteArray StreamingConverter::formatSSE(const QString &eventType, const QJsonObject &data)
{
    QByteArray result;
    result += "event: " + eventType.toUtf8() + "\n";
    result += "data: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\n\n";
    return result;
}

QJsonObject StreamingConverter::createMessageStartEvent(const QString &chunkId, const QString &model,
                                                         const StreamingState &state)
{
    QJsonObject event;
    event["type"] = QString(EVENT_MESSAGE_START);

    QJsonObject message;
    message["id"] = state.messageId.isEmpty() ? chunkId : state.messageId;
    message["type"] = QString("message");
    message["role"] = QString(ROLE_ASSISTANT);
    message["model"] = model;
    message["content"] = QJsonArray();
    message["stop_reason"] = QJsonValue::Null;
    message["stop_sequence"] = QJsonValue::Null;

    QJsonObject usage;
    // input_tokens minus cached_tokens
    usage["input_tokens"] = state.inputTokens - state.cacheReadTokens;
    usage["output_tokens"] = 0;
    if (state.cacheReadTokens > 0) {
        usage["cache_read_input_tokens"] = state.cacheReadTokens;
    }
    message["usage"] = usage;

    event["message"] = message;

    return event;
}

QJsonObject StreamingConverter::createTextBlockStartEvent(int index)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_START);
    event["index"] = index;

    QJsonObject contentBlock;
    contentBlock["type"] = QString(CONTENT_TEXT);
    contentBlock["text"] = QString("");
    event["content_block"] = contentBlock;

    return event;
}

QJsonObject StreamingConverter::createToolBlockStartEvent(int index, const QString &toolId, const QString &name)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_START);
    event["index"] = index;

    QJsonObject contentBlock;
    contentBlock["type"] = QString(CONTENT_TOOL_USE);
    contentBlock["id"] = toolId;
    contentBlock["name"] = name;
    contentBlock["input"] = QJsonObject();
    event["content_block"] = contentBlock;

    return event;
}

QJsonObject StreamingConverter::createThinkingBlockStartEvent(int index)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_START);
    event["index"] = index;

    QJsonObject contentBlock;
    contentBlock["type"] = QString(CONTENT_THINKING);
    contentBlock["thinking"] = QString("");
    event["content_block"] = contentBlock;

    return event;
}

QJsonObject StreamingConverter::createTextDeltaEvent(int index, const QString &text)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_DELTA);
    event["index"] = index;

    QJsonObject delta;
    delta["type"] = QString(DELTA_TEXT);
    delta["text"] = text;
    event["delta"] = delta;

    return event;
}

QJsonObject StreamingConverter::createThinkingDeltaEvent(int index, const QString &thinking)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_DELTA);
    event["index"] = index;

    QJsonObject delta;
    delta["type"] = QString(DELTA_THINKING);
    delta["thinking"] = thinking;
    event["delta"] = delta;

    return event;
}

QJsonObject StreamingConverter::createInputJsonDeltaEvent(int index, const QString &partialJson)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_DELTA);
    event["index"] = index;

    QJsonObject delta;
    delta["type"] = QString(DELTA_INPUT_JSON);
    delta["partial_json"] = partialJson;
    event["delta"] = delta;

    return event;
}

QJsonObject StreamingConverter::createBlockStopEvent(int index)
{
    QJsonObject event;
    event["type"] = QString(EVENT_CONTENT_BLOCK_STOP);
    event["index"] = index;

    return event;
}

QJsonObject StreamingConverter::createMessageDeltaEvent(const QString &stopReason, const StreamingState &state)
{
    QJsonObject event;
    event["type"] = QString(EVENT_MESSAGE_DELTA);

    QJsonObject delta;
    delta["stop_reason"] = stopReason;
    delta["stop_sequence"] = QJsonValue::Null;
    event["delta"] = delta;

    QJsonObject usage;
    usage["input_tokens"] = state.inputTokens - state.cacheReadTokens;
    usage["output_tokens"] = state.outputTokens;
    if (state.cacheReadTokens > 0) {
        usage["cache_read_input_tokens"] = state.cacheReadTokens;
    }
    event["usage"] = usage;

    return event;
}

QJsonObject StreamingConverter::createMessageStopEvent()
{
    QJsonObject event;
    event["type"] = QString(EVENT_MESSAGE_STOP);

    return event;
}

QJsonObject StreamingConverter::createErrorEvent(const QString &errorType, const QString &message)
{
    QJsonObject event;
    event["type"] = QString(EVENT_ERROR);

    QJsonObject error;
    error["type"] = errorType;
    error["message"] = message;
    event["error"] = error;

    return event;
}

} // namespace Conversion
