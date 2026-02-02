#include "request_converter.h"
#include "constants.h"
#include "../logger.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace Conversion {

QByteArray RequestConverter::convert(const QByteArray &claudeRequest, QString &originalModel)
{
    // Log incoming Claude request
    LOG("=== RequestConverter: INCOMING CLAUDE REQUEST ===");
    QString claudeReqStr = QString::fromUtf8(claudeRequest);
    if (claudeReqStr.length() > 3000) {
        LOG(QString("RequestConverter: Claude request (truncated): %1...").arg(claudeReqStr.left(3000)));
    } else {
        LOG(QString("RequestConverter: Claude request: %1").arg(claudeReqStr));
    }
    LOG("=================================================");

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(claudeRequest, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG("RequestConverter: Failed to parse Claude request");
        return claudeRequest;
    }

    QJsonObject claudeObj = doc.object();
    QJsonObject openAIObj;

    // Extract and set model
    originalModel = claudeObj.value("model").toString();
    openAIObj["model"] = originalModel;

    // max_tokens
    if (claudeObj.contains("max_tokens")) {
        openAIObj["max_tokens"] = claudeObj.value("max_tokens").toInt();
    }

    // Temperature: use provided value or default
    if (claudeObj.contains("temperature")) {
        openAIObj["temperature"] = claudeObj.value("temperature").toDouble();
    } else {
        openAIObj["temperature"] = DEFAULT_TEMPERATURE;
        LOG(QString("RequestConverter: Using default temperature=%1").arg(DEFAULT_TEMPERATURE));
    }

    // top_p
    if (claudeObj.contains("top_p")) {
        openAIObj["top_p"] = claudeObj.value("top_p").toDouble();
    }

    // stop_sequences -> stop
    if (claudeObj.contains("stop_sequences")) {
        openAIObj["stop"] = claudeObj.value("stop_sequences");
    }

    // stream with stream_options
    if (claudeObj.contains("stream")) {
        bool isStreaming = claudeObj.value("stream").toBool();
        openAIObj["stream"] = isStreaming;

        if (isStreaming) {
            QJsonObject streamOptions;
            streamOptions["include_usage"] = true;
            openAIObj["stream_options"] = streamOptions;
        }
    }

    // Note: Claude's thinking parameter is NOT converted
    // because it's not a standard OpenAI field and causes errors on many backends
    if (claudeObj.contains("thinking")) {
        LOG("RequestConverter: Skipping thinking parameter (not standard OpenAI)");
    }

    // Extract system prompt and convert messages
    QString systemPrompt = extractSystemPrompt(claudeObj);
    QJsonArray claudeMessages = claudeObj.value("messages").toArray();
    openAIObj["messages"] = convertMessages(claudeMessages, systemPrompt);

    // Convert tools
    if (claudeObj.contains("tools")) {
        QJsonArray tools = claudeObj.value("tools").toArray();
        QJsonArray openAITools = convertTools(tools);
        if (!openAITools.isEmpty()) {
            openAIObj["tools"] = openAITools;
        }
    }

    // Convert tool_choice
    if (claudeObj.contains("tool_choice")) {
        QJsonObject toolChoice = claudeObj.value("tool_choice").toObject();
        openAIObj["tool_choice"] = convertToolChoice(toolChoice);
    }

    QJsonDocument newDoc(openAIObj);
    QByteArray result = newDoc.toJson(QJsonDocument::Compact);

    // Log outgoing OpenAI request
    LOG("=== RequestConverter: OUTGOING OPENAI REQUEST ===");
    QString openAIReqStr = QString::fromUtf8(result);
    if (openAIReqStr.length() > 3000) {
        LOG(QString("RequestConverter: OpenAI request (truncated): %1...").arg(openAIReqStr.left(3000)));
    } else {
        LOG(QString("RequestConverter: OpenAI request: %1").arg(openAIReqStr));
    }
    LOG("=================================================");

    LOG(QString("RequestConverter: Converted request (%1 bytes -> %2 bytes)")
            .arg(claudeRequest.size()).arg(result.size()));

    return result;
}

QString RequestConverter::convertEndpoint(const QString &claudeEndpoint)
{
    if (claudeEndpoint.contains("/messages/count_tokens")) {
        return "/v1/chat/completions";
    }
    if (claudeEndpoint.contains("/messages")) {
        QString converted = claudeEndpoint;
        converted.replace("/messages", "/chat/completions");
        return converted;
    }
    return claudeEndpoint;
}

bool RequestConverter::isTokenCountEndpoint(const QString &endpoint)
{
    return endpoint.contains("/count_tokens");
}

QByteArray RequestConverter::convertTokenCountRequest(const QByteArray &claudeRequest, QString &originalModel)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(claudeRequest, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG("RequestConverter: Failed to parse token count request");
        return claudeRequest;
    }

    QJsonObject claudeObj = doc.object();
    QJsonObject openAIObj;

    originalModel = claudeObj.value("model").toString();
    openAIObj["model"] = originalModel;
    openAIObj["max_tokens"] = 1;  // Minimize cost
    openAIObj["stream"] = false;

    QString systemPrompt = extractSystemPrompt(claudeObj);
    QJsonArray claudeMessages = claudeObj.value("messages").toArray();
    openAIObj["messages"] = convertMessages(claudeMessages, systemPrompt);

    if (claudeObj.contains("tools")) {
        openAIObj["tools"] = convertTools(claudeObj.value("tools").toArray());
    }

    return QJsonDocument(openAIObj).toJson(QJsonDocument::Compact);
}

int RequestConverter::estimateTokenCount(const QByteArray &claudeRequest)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(claudeRequest, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return claudeRequest.size() / 4;  // Fallback
    }

    QJsonObject claudeObj = doc.object();
    int totalTokens = 0;

    // System prompt tokens
    QString systemPrompt = extractSystemPrompt(claudeObj);
    if (!systemPrompt.isEmpty()) {
        totalTokens += estimateTextTokens(systemPrompt);
        totalTokens += 4;  // Overhead
    }

    // Message tokens
    QJsonArray messages = claudeObj.value("messages").toArray();
    for (const QJsonValue &msgVal : messages) {
        QJsonObject msg = msgVal.toObject();
        totalTokens += 4;  // Message overhead

        QJsonValue contentVal = msg.value("content");
        if (contentVal.isString()) {
            totalTokens += estimateTextTokens(contentVal.toString());
        } else if (contentVal.isArray()) {
            QJsonArray contentArray = contentVal.toArray();
            for (const QJsonValue &blockVal : contentArray) {
                QJsonObject block = blockVal.toObject();
                QString type = block.value("type").toString();

                if (type == CONTENT_TEXT) {
                    totalTokens += estimateTextTokens(block.value("text").toString());
                } else if (type == CONTENT_THINKING) {
                    totalTokens += estimateTextTokens(block.value("thinking").toString());
                } else if (type == CONTENT_TOOL_USE) {
                    totalTokens += estimateTextTokens(block.value("name").toString());
                    QJsonDocument inputDoc(block.value("input").toObject());
                    totalTokens += estimateTextTokens(QString::fromUtf8(inputDoc.toJson(QJsonDocument::Compact)));
                    totalTokens += 10;  // Tool call overhead
                } else if (type == CONTENT_TOOL_RESULT) {
                    totalTokens += estimateTextTokens(parseToolResultContent(block.value("content")));
                    totalTokens += 6;  // Tool result overhead
                } else if (type == CONTENT_IMAGE) {
                    totalTokens += 765;  // Approximate for medium image
                }
            }
        }
    }

    // Tool definition tokens
    if (claudeObj.contains("tools")) {
        QJsonArray tools = claudeObj.value("tools").toArray();
        for (const QJsonValue &toolVal : tools) {
            QJsonObject tool = toolVal.toObject();
            totalTokens += estimateTextTokens(tool.value("name").toString());
            totalTokens += estimateTextTokens(tool.value("description").toString());
            if (tool.contains("input_schema")) {
                QJsonDocument schemaDoc(tool.value("input_schema").toObject());
                totalTokens += estimateTextTokens(QString::fromUtf8(schemaDoc.toJson(QJsonDocument::Compact)));
            }
            totalTokens += 8;  // Tool definition overhead
        }
    }

    totalTokens += 3;  // Base overhead

    LOG(QString("RequestConverter: Estimated token count: %1").arg(totalTokens));
    return totalTokens;
}

QString RequestConverter::extractSystemPrompt(const QJsonObject &claudeObj)
{
    QString systemPrompt;
    QJsonValue systemVal = claudeObj.value("system");

    if (systemVal.isString()) {
        systemPrompt = systemVal.toString();
    } else if (systemVal.isArray()) {
        QJsonArray systemArray = systemVal.toArray();
        QStringList textParts;
        for (const QJsonValue &blockVal : systemArray) {
            QJsonObject block = blockVal.toObject();
            if (block.value("type").toString() == CONTENT_TEXT) {
                textParts.append(block.value("text").toString());
            }
        }
        systemPrompt = textParts.join("\n\n");
    }

    return systemPrompt.trimmed();
}

// Following the TS implementation pattern exactly:
// Each Claude message is expanded to one or more OpenAI messages IN ORDER
QJsonArray RequestConverter::convertMessages(const QJsonArray &claudeMessages, const QString &systemPrompt)
{
    QJsonArray openAIMessages;

    // Add system message first if present
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg["role"] = QString(ROLE_SYSTEM);
        sysMsg["content"] = systemPrompt;
        openAIMessages.append(sysMsg);
    }

    LOG(QString("RequestConverter: Converting %1 Claude messages").arg(claudeMessages.size()));

    // Process each message IN ORDER - flatMap pattern from TS
    for (const QJsonValue &msgVal : claudeMessages) {
        QJsonObject claudeMsg = msgVal.toObject();
        QString role = claudeMsg.value("role").toString();

        QJsonArray expandedMessages;
        if (role == ROLE_USER) {
            expandedMessages = handleUserMessage(claudeMsg);
        } else if (role == ROLE_ASSISTANT) {
            expandedMessages = handleAssistantMessage(claudeMsg);
        }

        // Append all expanded messages
        for (const QJsonValue &expanded : expandedMessages) {
            openAIMessages.append(expanded);
        }
    }

    LOG(QString("RequestConverter: Total OpenAI messages: %1").arg(openAIMessages.size()));
    return openAIMessages;
}

// Handle user message - extracts tool_result blocks first, then other content
// This maintains the protocol: assistant(tool_calls) -> tool(results) -> user(content)
QJsonArray RequestConverter::handleUserMessage(const QJsonObject &claudeMsg)
{
    QJsonArray newMessages;
    QJsonValue contentVal = claudeMsg.value("content");

    if (contentVal.isNull()) {
        QJsonObject msg;
        msg["role"] = QString(ROLE_USER);
        msg["content"] = QString("");
        newMessages.append(msg);
        return newMessages;
    }

    if (contentVal.isString()) {
        QJsonObject msg;
        msg["role"] = QString(ROLE_USER);
        msg["content"] = contentVal.toString();
        newMessages.append(msg);
        return newMessages;
    }

    if (contentVal.isArray()) {
        QJsonArray contentArray = contentVal.toArray();

        // Separate tool_result blocks from other blocks
        QJsonArray toolResultBlocks;
        QJsonArray otherBlocks;

        for (const QJsonValue &blockVal : contentArray) {
            QJsonObject block = blockVal.toObject();
            if (block.value("type").toString() == CONTENT_TOOL_RESULT) {
                toolResultBlocks.append(block);
            } else {
                otherBlocks.append(block);
            }
        }

        // Tool results must come FIRST to maintain protocol
        for (const QJsonValue &blockVal : toolResultBlocks) {
            QJsonObject block = blockVal.toObject();
            QString toolUseId = block.value("tool_use_id").toString();
            QString content = parseToolResultContent(block.value("content"));

            QJsonObject toolMsg;
            toolMsg["role"] = QString(ROLE_TOOL);
            toolMsg["tool_call_id"] = toolUseId;
            toolMsg["content"] = content;
            newMessages.append(toolMsg);

            LOG(QString("RequestConverter: Added tool message for tool_call_id=%1").arg(toolUseId));
        }

        // Then add user message with other content (if any)
        if (!otherBlocks.isEmpty()) {
            QJsonObject userMsg;
            userMsg["role"] = QString(ROLE_USER);
            userMsg["content"] = mapUserContent(otherBlocks);
            newMessages.append(userMsg);
        }
    }

    return newMessages;
}

// Handle assistant message - extracts tool_use blocks into tool_calls
QJsonArray RequestConverter::handleAssistantMessage(const QJsonObject &claudeMsg)
{
    QJsonArray newMessages;
    QJsonValue contentVal = claudeMsg.value("content");

    if (contentVal.isNull()) {
        QJsonObject msg;
        msg["role"] = QString(ROLE_ASSISTANT);
        msg["content"] = QJsonValue::Null;
        newMessages.append(msg);
        return newMessages;
    }

    if (contentVal.isString()) {
        QJsonObject msg;
        msg["role"] = QString(ROLE_ASSISTANT);
        msg["content"] = contentVal.toString();
        newMessages.append(msg);
        return newMessages;
    }

    if (contentVal.isArray()) {
        QJsonArray contentArray = contentVal.toArray();

        // Separate blocks by type
        QJsonArray toolUseBlocks;
        QStringList textParts;

        for (const QJsonValue &blockVal : contentArray) {
            QJsonObject block = blockVal.toObject();
            QString type = block.value("type").toString();

            if (type == CONTENT_TOOL_USE) {
                toolUseBlocks.append(block);
            } else if (type == CONTENT_TEXT) {
                textParts.append(block.value("text").toString());
            } else if (type == CONTENT_THINKING) {
                // Combine thinking with text content (OpenAI doesn't have separate thinking)
                textParts.append(block.value("thinking").toString());
            }
        }

        QString allTextContent = textParts.join("\n\n");

        if (!toolUseBlocks.isEmpty()) {
            // Assistant message with tool_calls
            QJsonObject assistantMsg;
            assistantMsg["role"] = QString(ROLE_ASSISTANT);

            if (!allTextContent.isEmpty()) {
                assistantMsg["content"] = allTextContent;
            } else {
                assistantMsg["content"] = QJsonValue::Null;
            }

            // Convert tool_use blocks to tool_calls array
            QJsonArray toolCalls;
            for (const QJsonValue &blockVal : toolUseBlocks) {
                QJsonObject block = blockVal.toObject();

                QJsonObject toolCall;
                toolCall["id"] = block.value("id").toString();
                toolCall["type"] = QString(TOOL_FUNCTION);

                QJsonObject function;
                function["name"] = block.value("name").toString();
                function["arguments"] = QString::fromUtf8(
                    QJsonDocument(block.value("input").toObject()).toJson(QJsonDocument::Compact));
                toolCall["function"] = function;

                toolCalls.append(toolCall);

                LOG(QString("RequestConverter: Added tool_call id=%1 name=%2")
                        .arg(block.value("id").toString(), block.value("name").toString()));
            }

            assistantMsg["tool_calls"] = toolCalls;
            newMessages.append(assistantMsg);
        } else {
            // Simple assistant message
            QJsonObject assistantMsg;
            assistantMsg["role"] = QString(ROLE_ASSISTANT);
            assistantMsg["content"] = allTextContent.isEmpty() ? QJsonValue::Null : QJsonValue(allTextContent);
            newMessages.append(assistantMsg);
        }
    }

    return newMessages;
}

// Map user content blocks to OpenAI format
QJsonValue RequestConverter::mapUserContent(const QJsonArray &blocks)
{
    bool hasImage = false;
    for (const QJsonValue &blockVal : blocks) {
        if (blockVal.toObject().value("type").toString() == CONTENT_IMAGE) {
            hasImage = true;
            break;
        }
    }

    if (!hasImage) {
        // No images - return as simple string
        QStringList textParts;
        for (const QJsonValue &blockVal : blocks) {
            QJsonObject block = blockVal.toObject();
            QString type = block.value("type").toString();
            if (type == CONTENT_TEXT) {
                textParts.append(block.value("text").toString());
            } else if (type == CONTENT_THINKING) {
                textParts.append(block.value("thinking").toString());
            }
        }
        return textParts.join("\n\n");
    }

    // Has images - return as content array
    QJsonArray contentParts;
    for (const QJsonValue &blockVal : blocks) {
        QJsonObject block = blockVal.toObject();
        QString type = block.value("type").toString();

        if (type == CONTENT_TEXT) {
            QJsonObject textPart;
            textPart["type"] = QString("text");
            textPart["text"] = block.value("text").toString();
            contentParts.append(textPart);
        } else if (type == CONTENT_THINKING) {
            QJsonObject textPart;
            textPart["type"] = QString("text");
            textPart["text"] = block.value("thinking").toString();
            contentParts.append(textPart);
        } else if (type == CONTENT_IMAGE) {
            QJsonObject source = block.value("source").toObject();
            if (source.value("type").toString() == "base64") {
                QString mediaType = source.value("media_type").toString();
                QString data = source.value("data").toString();

                QJsonObject imageUrl;
                imageUrl["url"] = QString("data:%1;base64,%2").arg(mediaType, data);

                QJsonObject imagePart;
                imagePart["type"] = QString("image_url");
                imagePart["image_url"] = imageUrl;
                contentParts.append(imagePart);
            }
        }
    }

    return contentParts;
}

// Legacy functions kept for compatibility
QJsonObject RequestConverter::convertUserMessage(const QJsonObject &claudeMsg)
{
    QJsonArray messages = handleUserMessage(claudeMsg);
    if (messages.isEmpty()) {
        QJsonObject emptyMsg;
        emptyMsg["role"] = QString(ROLE_USER);
        emptyMsg["content"] = QString("");
        return emptyMsg;
    }
    // Return last message (the actual user content)
    return messages.last().toObject();
}

QJsonObject RequestConverter::convertAssistantMessage(const QJsonObject &claudeMsg)
{
    QJsonArray messages = handleAssistantMessage(claudeMsg);
    if (messages.isEmpty()) {
        QJsonObject emptyMsg;
        emptyMsg["role"] = QString(ROLE_ASSISTANT);
        emptyMsg["content"] = QJsonValue::Null;
        return emptyMsg;
    }
    return messages.first().toObject();
}

QJsonArray RequestConverter::convertToolResults(const QJsonObject &claudeMsg)
{
    QJsonArray toolMessages;
    QJsonValue contentVal = claudeMsg.value("content");

    if (!contentVal.isArray()) {
        return toolMessages;
    }

    QJsonArray contentArray = contentVal.toArray();
    for (const QJsonValue &blockVal : contentArray) {
        QJsonObject block = blockVal.toObject();
        if (block.value("type").toString() == CONTENT_TOOL_RESULT) {
            QString content = parseToolResultContent(block.value("content"));

            QJsonObject toolMsg;
            toolMsg["role"] = QString(ROLE_TOOL);
            toolMsg["tool_call_id"] = block.value("tool_use_id").toString();
            toolMsg["content"] = content;
            toolMessages.append(toolMsg);
        }
    }

    return toolMessages;
}

QJsonArray RequestConverter::convertTools(const QJsonArray &claudeTools)
{
    QJsonArray openAITools;

    for (const QJsonValue &toolVal : claudeTools) {
        QJsonObject claudeTool = toolVal.toObject();
        QString name = claudeTool.value("name").toString();

        if (name.isEmpty() || name.trimmed().isEmpty()) {
            continue;
        }

        QJsonObject openAITool;
        openAITool["type"] = QString(TOOL_FUNCTION);

        QJsonObject function;
        function["name"] = name;
        function["description"] = claudeTool.value("description").toString();

        if (claudeTool.contains("input_schema")) {
            // Sanitize the schema to remove unsupported keywords like 'const'
            QJsonValue sanitized = sanitizeJsonSchema(claudeTool.value("input_schema"));
            function["parameters"] = sanitized;
        }

        openAITool["function"] = function;
        openAITools.append(openAITool);
    }

    return openAITools;
}

QJsonValue RequestConverter::convertToolChoice(const QJsonObject &claudeToolChoice)
{
    QString type = claudeToolChoice.value("type").toString();

    if (type == "auto") {
        return QString("auto");
    } else if (type == "any") {
        return QString("required");  // OpenAI equivalent
    } else if (type == "none") {
        return QString("none");
    } else if (type == "tool" && claudeToolChoice.contains("name")) {
        QJsonObject funcChoice;
        funcChoice["type"] = QString(TOOL_FUNCTION);

        QJsonObject funcName;
        funcName["name"] = claudeToolChoice.value("name").toString();
        funcChoice["function"] = funcName;

        return funcChoice;
    }

    return QString("auto");
}

QString RequestConverter::parseToolResultContent(const QJsonValue &content)
{
    if (content.isNull()) {
        return QString("No content provided");
    }

    if (content.isString()) {
        return content.toString();
    }

    if (content.isArray()) {
        QStringList resultParts;
        QJsonArray contentArray = content.toArray();
        for (const QJsonValue &item : contentArray) {
            if (item.isString()) {
                resultParts.append(item.toString());
            } else if (item.isObject()) {
                QJsonObject obj = item.toObject();
                if (obj.value("type").toString() == CONTENT_TEXT) {
                    resultParts.append(obj.value("text").toString());
                } else if (obj.contains("text")) {
                    resultParts.append(obj.value("text").toString());
                } else {
                    resultParts.append(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
                }
            }
        }
        return resultParts.join("\n").trimmed();
    }

    if (content.isObject()) {
        QJsonObject obj = content.toObject();
        if (obj.value("type").toString() == CONTENT_TEXT) {
            return obj.value("text").toString();
        }
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    return QString("Unparseable content");
}

int RequestConverter::estimateTextTokens(const QString &text)
{
    if (text.isEmpty()) return 0;
    // Use 3.7 as average (between 3.5 for code and 4.0 for English)
    return qMax(1, static_cast<int>(text.length() / 3.7));
}

QJsonValue RequestConverter::sanitizeJsonSchema(const QJsonValue &schema)
{
    if (schema.isNull() || schema.isUndefined()) {
        return schema;
    }

    if (schema.isArray()) {
        QJsonArray result;
        QJsonArray arr = schema.toArray();
        for (const QJsonValue &item : arr) {
            result.append(sanitizeJsonSchema(item));
        }
        return result;
    }

    if (!schema.isObject()) {
        return schema;
    }

    QJsonObject obj = schema.toObject();
    QJsonObject result;

    for (auto it = obj.begin(); it != obj.end(); ++it) {
        QString key = it.key();
        QJsonValue value = it.value();

        // Convert 'const' to 'enum' with single value
        if (key == "const") {
            QJsonArray enumArray;
            enumArray.append(value);
            result["enum"] = enumArray;
            LOG(QString("RequestConverter: Converted 'const' to 'enum' for value: %1")
                    .arg(value.isString() ? value.toString() : "non-string"));
        }
        // Handle 'properties', 'definitions', '$defs', 'patternProperties' - each value is a schema
        else if (key == "properties" || key == "definitions" || key == "$defs" || key == "patternProperties") {
            if (value.isObject()) {
                QJsonObject props = value.toObject();
                QJsonObject sanitizedProps;
                for (auto propIt = props.begin(); propIt != props.end(); ++propIt) {
                    sanitizedProps[propIt.key()] = sanitizeJsonSchema(propIt.value());
                }
                result[key] = sanitizedProps;
            } else {
                result[key] = value;
            }
        }
        // These are schema values that need direct recursion
        else if (key == "items" || key == "additionalProperties" ||
                 key == "anyOf" || key == "oneOf" || key == "allOf" || key == "not" ||
                 key == "if" || key == "then" || key == "else" ||
                 key == "contains" || key == "propertyNames" ||
                 key == "additionalItems" || key == "unevaluatedItems" || key == "unevaluatedProperties") {
            result[key] = sanitizeJsonSchema(value);
        }
        // Keep other keys as-is (type, description, enum, format, default, etc.)
        else {
            result[key] = value;
        }
    }

    return result;
}

} // namespace Conversion
