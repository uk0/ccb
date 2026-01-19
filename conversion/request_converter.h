#ifndef CONVERSION_REQUEST_CONVERTER_H
#define CONVERSION_REQUEST_CONVERTER_H

#include <QByteArray>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

namespace Conversion {

/**
 * RequestConverter - Converts Claude API requests to OpenAI format.
 *
 * Following the pattern from copilot-api TypeScript implementation
 * for clear, maintainable code with correct message ordering.
 *
 * Key pattern: Each Claude message is expanded to one or more OpenAI messages
 * in order, maintaining the protocol:
 *   assistant(tool_calls) -> tool(results) -> user(content)
 */
class RequestConverter {
public:
    /**
     * Convert a complete Claude API request to OpenAI format.
     *
     * @param claudeRequest The Claude format request body
     * @param originalModel Output parameter to store the original model name
     * @return OpenAI format request body
     */
    static QByteArray convert(const QByteArray &claudeRequest, QString &originalModel);

    /**
     * Convert endpoint path from Claude format to OpenAI format.
     */
    static QString convertEndpoint(const QString &claudeEndpoint);

    /**
     * Check if the endpoint is a token counting endpoint.
     */
    static bool isTokenCountEndpoint(const QString &endpoint);

    /**
     * Convert a Claude token count request to an OpenAI request.
     * Uses max_tokens=1 to minimize cost while still getting token count.
     */
    static QByteArray convertTokenCountRequest(const QByteArray &claudeRequest, QString &originalModel);

    /**
     * Estimate token count locally without API call.
     * Uses simplified tiktoken-like estimation (~3.7 chars per token).
     */
    static int estimateTokenCount(const QByteArray &claudeRequest);

private:
    /**
     * Extract system prompt from Claude request.
     * Handles both string and array formats.
     */
    static QString extractSystemPrompt(const QJsonObject &claudeObj);

    /**
     * Convert Claude messages array to OpenAI format.
     * Uses flatMap pattern - each message expands to one or more OpenAI messages.
     */
    static QJsonArray convertMessages(const QJsonArray &claudeMessages, const QString &systemPrompt);

    /**
     * Handle user message - expands to tool messages + optional user message.
     * Tool results come FIRST to maintain protocol ordering.
     */
    static QJsonArray handleUserMessage(const QJsonObject &claudeMsg);

    /**
     * Handle assistant message - extracts tool_use into tool_calls.
     */
    static QJsonArray handleAssistantMessage(const QJsonObject &claudeMsg);

    /**
     * Map user content blocks to OpenAI format.
     * Returns string for text-only, array for multimodal.
     */
    static QJsonValue mapUserContent(const QJsonArray &blocks);

    /**
     * Convert a single Claude user message to OpenAI format (legacy).
     */
    static QJsonObject convertUserMessage(const QJsonObject &claudeMsg);

    /**
     * Convert a single Claude assistant message to OpenAI format (legacy).
     */
    static QJsonObject convertAssistantMessage(const QJsonObject &claudeMsg);

    /**
     * Convert Claude tool results to OpenAI tool messages.
     */
    static QJsonArray convertToolResults(const QJsonObject &claudeMsg);

    /**
     * Convert Claude tool definitions to OpenAI function format.
     */
    static QJsonArray convertTools(const QJsonArray &claudeTools);

    /**
     * Convert Claude tool_choice to OpenAI format.
     */
    static QJsonValue convertToolChoice(const QJsonObject &claudeToolChoice);

    /**
     * Parse tool result content into a string.
     * Handles string, array, and object formats.
     */
    static QString parseToolResultContent(const QJsonValue &content);

    /**
     * Estimate tokens for a text string.
     * Uses ~3.7 chars per token (average between code and English).
     */
    static int estimateTextTokens(const QString &text);
};

} // namespace Conversion

#endif // CONVERSION_REQUEST_CONVERTER_H
