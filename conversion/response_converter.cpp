#include "response_converter.h"
#include "constants.h"
#include "../logger.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUuid>

namespace Conversion {

QByteArray ResponseConverter::convert(const QByteArray &openAIResponse, const QString &originalModel)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(openAIResponse, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG("ResponseConverter: Failed to parse OpenAI response");
        return openAIResponse;
    }

    QJsonObject openAIObj = doc.object();

    // Handle error responses
    if (openAIObj.contains("error")) {
        QJsonObject errorObj = openAIObj.value("error").toObject();
        return createErrorResponse("api_error", errorObj.value("message").toString());
    }

    // Extract response data
    QJsonArray choices = openAIObj.value("choices").toArray();
    if (choices.isEmpty()) {
        return createErrorResponse("api_error", "No choices in OpenAI response");
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject message = choice.value("message").toObject();

    // Build Claude content blocks
    QJsonArray contentBlocks;

    // Handle reasoning_content (DeepSeek, other providers)
    if (message.contains("reasoning_content")) {
        QString reasoningContent = message.value("reasoning_content").toString();
        if (!reasoningContent.isEmpty()) {
            QJsonObject thinkingBlock;
            thinkingBlock["type"] = QString(CONTENT_THINKING);
            thinkingBlock["thinking"] = reasoningContent;
            contentBlocks.append(thinkingBlock);
        }
    }

    // Handle thinking object (OpenAI extended thinking format)
    if (message.contains("thinking")) {
        QJsonValue thinkingVal = message.value("thinking");
        if (thinkingVal.isObject()) {
            QJsonObject thinkingObj = thinkingVal.toObject();
            QString content = thinkingObj.value("content").toString();
            QString signature = thinkingObj.value("signature").toString();

            if (!content.isEmpty()) {
                QJsonObject thinkingBlock;
                thinkingBlock["type"] = QString(CONTENT_THINKING);
                thinkingBlock["thinking"] = content;
                if (!signature.isEmpty()) {
                    thinkingBlock["signature"] = signature;
                }
                contentBlocks.append(thinkingBlock);
            }
        }
    }

    // Add text content
    QString textContent = message.value("content").toString();
    if (!textContent.isEmpty()) {
        QJsonObject textBlock;
        textBlock["type"] = QString(CONTENT_TEXT);
        textBlock["text"] = textContent;
        contentBlocks.append(textBlock);
    }

    // Add tool calls
    QJsonArray toolCalls = message.value("tool_calls").toArray();
    if (!toolCalls.isEmpty()) {
        QJsonArray toolUseBlocks = convertToolCalls(toolCalls);
        for (const QJsonValue &block : toolUseBlocks) {
            contentBlocks.append(block);
        }
    }

    // Ensure at least one content block
    if (contentBlocks.isEmpty()) {
        QJsonObject textBlock;
        textBlock["type"] = QString(CONTENT_TEXT);
        textBlock["text"] = QString("");
        contentBlocks.append(textBlock);
    }

    // Map stop reason
    QString finishReason = choice.value("finish_reason").toString();
    bool hasToolCalls = !toolCalls.isEmpty();
    QString stopReason = mapStopReason(finishReason, hasToolCalls);

    if (hasToolCalls && stopReason != STOP_TOOL_USE) {
        LOG("ResponseConverter: Overriding stop_reason to tool_use (tool_calls present)");
        stopReason = STOP_TOOL_USE;
    }

    // Build Claude response
    QJsonObject claudeResponse;
    claudeResponse["id"] = generateMessageId();
    claudeResponse["type"] = QString("message");
    claudeResponse["role"] = QString(ROLE_ASSISTANT);
    claudeResponse["model"] = originalModel;
    claudeResponse["content"] = contentBlocks;
    claudeResponse["stop_reason"] = stopReason;
    claudeResponse["stop_sequence"] = QJsonValue::Null;

    // Usage
    QJsonObject usage;
    QJsonObject openAIUsage = openAIObj.value("usage").toObject();
    usage["input_tokens"] = openAIUsage.value("prompt_tokens").toInt(0);
    usage["output_tokens"] = openAIUsage.value("completion_tokens").toInt(0);
    claudeResponse["usage"] = usage;

    return QJsonDocument(claudeResponse).toJson(QJsonDocument::Compact);
}

QByteArray ResponseConverter::convertTokenCountResponse(const QByteArray &openAIResponse)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(openAIResponse, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return createLocalTokenCountResponse(0);
    }

    QJsonObject openAIObj = doc.object();

    // Handle error
    if (openAIObj.contains("error")) {
        QJsonObject errorObj = openAIObj.value("error").toObject();
        return createErrorResponse("api_error", errorObj.value("message").toString());
    }

    // Extract token count
    QJsonObject usage = openAIObj.value("usage").toObject();
    int inputTokens = usage.value("prompt_tokens").toInt(0);

    return createLocalTokenCountResponse(inputTokens);
}

QByteArray ResponseConverter::createLocalTokenCountResponse(int tokenCount)
{
    QJsonObject response;
    response["input_tokens"] = tokenCount;
    LOG(QString("ResponseConverter: Token count response: %1 tokens").arg(tokenCount));
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QByteArray ResponseConverter::createErrorResponse(const QString &errorType, const QString &message)
{
    QJsonObject claudeError;
    claudeError["type"] = QString("error");

    QJsonObject errorDetail;
    errorDetail["type"] = errorType;
    errorDetail["message"] = message;
    claudeError["error"] = errorDetail;

    return QJsonDocument(claudeError).toJson(QJsonDocument::Compact);
}

QString ResponseConverter::generateMessageId()
{
    return QString("msg_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(24));
}

QString ResponseConverter::mapStopReason(const QString &finishReason, bool hasToolCalls)
{
    // If tool calls present, always return tool_use
    if (hasToolCalls) {
        return QString(STOP_TOOL_USE);
    }

    if (finishReason == FINISH_STOP) {
        return QString(STOP_END_TURN);
    } else if (finishReason == FINISH_LENGTH) {
        return QString(STOP_MAX_TOKENS);
    } else if (finishReason == FINISH_TOOL_CALLS || finishReason == FINISH_FUNCTION_CALL) {
        return QString(STOP_TOOL_USE);
    } else if (finishReason == FINISH_CONTENT_FILTER) {
        LOG("ResponseConverter: Content filtered by backend");
        return QString(STOP_END_TURN);
    }

    return QString(STOP_END_TURN);
}

QJsonArray ResponseConverter::convertToolCalls(const QJsonArray &toolCalls)
{
    QJsonArray toolUseBlocks;

    for (const QJsonValue &tcVal : toolCalls) {
        QJsonObject tc = tcVal.toObject();

        if (tc.value("type").toString() != TOOL_FUNCTION) {
            continue;
        }

        QJsonObject function = tc.value("function").toObject();
        QString argumentsStr = function.value("arguments").toString();

        QJsonObject toolUse;
        toolUse["type"] = QString(CONTENT_TOOL_USE);
        toolUse["id"] = tc.value("id").toString();
        toolUse["name"] = function.value("name").toString();
        toolUse["input"] = parseArguments(argumentsStr);

        toolUseBlocks.append(toolUse);
    }

    return toolUseBlocks;
}

QJsonObject ResponseConverter::parseArguments(const QString &arguments)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(arguments.toUtf8(), &error);

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        // Return wrapper for invalid JSON
        QJsonObject wrapper;
        wrapper["raw_arguments"] = arguments;
        return wrapper;
    }

    return doc.object();
}

} // namespace Conversion
