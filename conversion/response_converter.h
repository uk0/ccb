#ifndef CONVERSION_RESPONSE_CONVERTER_H
#define CONVERSION_RESPONSE_CONVERTER_H

#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>

namespace Conversion {

/**
 * ResponseConverter - Converts OpenAI API responses to Claude format.
 *
 * Handles non-streaming responses. For streaming, see StreamingConverter.
 */
class ResponseConverter {
public:
    /**
     * Convert a complete OpenAI API response to Claude format.
     *
     * @param openAIResponse The OpenAI format response body
     * @param originalModel The original Claude model name to include in response
     * @return Claude format response body
     */
    static QByteArray convert(const QByteArray &openAIResponse, const QString &originalModel);

    /**
     * Convert OpenAI token count response to Claude format.
     */
    static QByteArray convertTokenCountResponse(const QByteArray &openAIResponse);

    /**
     * Create a local token count response (no API call).
     */
    static QByteArray createLocalTokenCountResponse(int tokenCount);

    /**
     * Create an error response in Claude format.
     */
    static QByteArray createErrorResponse(const QString &errorType, const QString &message);

private:
    /**
     * Generate a unique message ID for Claude format.
     */
    static QString generateMessageId();

    /**
     * Map OpenAI finish_reason to Claude stop_reason.
     * Also considers whether tool_calls are present.
     */
    static QString mapStopReason(const QString &finishReason, bool hasToolCalls);

    /**
     * Convert OpenAI tool_calls to Claude tool_use content blocks.
     */
    static QJsonArray convertToolCalls(const QJsonArray &toolCalls);

    /**
     * Parse function arguments JSON string into an object.
     * Returns raw_arguments wrapper if JSON is invalid.
     */
    static QJsonObject parseArguments(const QString &arguments);
};

} // namespace Conversion

#endif // CONVERSION_RESPONSE_CONVERTER_H
