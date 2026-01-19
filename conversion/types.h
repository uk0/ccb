#ifndef CONVERSION_TYPES_H
#define CONVERSION_TYPES_H

#include <QString>
#include <QMap>

namespace Conversion {

/**
 * StreamingState - State for streaming translation.
 *
 * Following the exact pattern from copilot-api TypeScript implementation:
 *
 * interface AnthropicStreamState {
 *   messageStartSent: boolean
 *   contentBlockIndex: number
 *   contentBlockOpen: boolean
 *   toolCalls: {
 *     [openAIToolIndex: number]: {
 *       id: string
 *       name: string
 *       anthropicBlockIndex: number
 *     }
 *   }
 * }
 */
struct StreamingState {
    // Whether message_start event has been sent
    bool messageStartSent = false;

    // Current content block index (increments when blocks are closed)
    int contentBlockIndex = 0;

    // Whether there's a content block currently open
    bool contentBlockOpen = false;

    // Tool call tracking - maps OpenAI tool index to info
    struct ToolCallInfo {
        QString id;
        QString name;
        int anthropicBlockIndex = -1;
    };
    QMap<int, ToolCallInfo> toolCalls;

    // Message ID (generated once per message)
    QString messageId;

    // Token counts from usage data
    int inputTokens = 0;
    int outputTokens = 0;
    int cacheReadTokens = 0;

    // Thinking block tracking (for reasoning_content from DeepSeek R1, etc.)
    bool thinkingBlockOpen = false;
    int thinkingBlockIndex = -1;

    // Check if any tool calls exist
    bool hasToolCalls() const {
        return !toolCalls.isEmpty();
    }

    // Check if the current open block is a tool block
    bool isToolBlockOpen() const {
        if (!contentBlockOpen) {
            return false;
        }
        // Check if current index corresponds to any known tool call
        for (auto it = toolCalls.begin(); it != toolCalls.end(); ++it) {
            if (it.value().anthropicBlockIndex == contentBlockIndex) {
                return true;
            }
        }
        return false;
    }

    // Check if the current open block is a thinking block
    bool isThinkingBlockOpen() const {
        return thinkingBlockOpen && thinkingBlockIndex >= 0;
    }

    // Reset state for new message
    void reset() {
        messageStartSent = false;
        contentBlockIndex = 0;
        contentBlockOpen = false;
        thinkingBlockOpen = false;
        thinkingBlockIndex = -1;
        toolCalls.clear();
        messageId.clear();
        inputTokens = 0;
        outputTokens = 0;
        cacheReadTokens = 0;
    }
};

/**
 * Token count result for local estimation.
 */
struct TokenCountResult {
    int inputTokens = 0;
    bool success = true;
    QString error;
};

} // namespace Conversion

#endif // CONVERSION_TYPES_H
