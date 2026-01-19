#ifndef CONVERSION_CONSTANTS_H
#define CONVERSION_CONSTANTS_H

#include <QString>

/**
 * Constants for API format conversion between Claude and OpenAI.
 * Following the pattern from claude-code-proxy for maintainability.
 */
namespace Conversion {

// Role constants
constexpr const char* ROLE_USER = "user";
constexpr const char* ROLE_ASSISTANT = "assistant";
constexpr const char* ROLE_SYSTEM = "system";
constexpr const char* ROLE_TOOL = "tool";

// Content type constants
constexpr const char* CONTENT_TEXT = "text";
constexpr const char* CONTENT_IMAGE = "image";
constexpr const char* CONTENT_TOOL_USE = "tool_use";
constexpr const char* CONTENT_TOOL_RESULT = "tool_result";
constexpr const char* CONTENT_THINKING = "thinking";

// Tool constants
constexpr const char* TOOL_FUNCTION = "function";

// Stop reason constants (Claude format)
constexpr const char* STOP_END_TURN = "end_turn";
constexpr const char* STOP_MAX_TOKENS = "max_tokens";
constexpr const char* STOP_TOOL_USE = "tool_use";
constexpr const char* STOP_ERROR = "error";

// OpenAI finish reasons
constexpr const char* FINISH_STOP = "stop";
constexpr const char* FINISH_LENGTH = "length";
constexpr const char* FINISH_TOOL_CALLS = "tool_calls";
constexpr const char* FINISH_FUNCTION_CALL = "function_call";
constexpr const char* FINISH_CONTENT_FILTER = "content_filter";

// SSE Event type constants
constexpr const char* EVENT_MESSAGE_START = "message_start";
constexpr const char* EVENT_MESSAGE_STOP = "message_stop";
constexpr const char* EVENT_MESSAGE_DELTA = "message_delta";
constexpr const char* EVENT_CONTENT_BLOCK_START = "content_block_start";
constexpr const char* EVENT_CONTENT_BLOCK_STOP = "content_block_stop";
constexpr const char* EVENT_CONTENT_BLOCK_DELTA = "content_block_delta";
constexpr const char* EVENT_PING = "ping";
constexpr const char* EVENT_ERROR = "error";

// Delta type constants
constexpr const char* DELTA_TEXT = "text_delta";
constexpr const char* DELTA_INPUT_JSON = "input_json_delta";
constexpr const char* DELTA_THINKING = "thinking_delta";
constexpr const char* DELTA_SIGNATURE = "signature_delta";

// Default values
constexpr double DEFAULT_TEMPERATURE = 0.1;

} // namespace Conversion

#endif // CONVERSION_CONSTANTS_H
