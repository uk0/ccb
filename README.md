# CCB - Claude Code Balance

A transparent proxy application for Claude Code API requests, built with Qt6. Supports multi-URL/Key automatic switching, error retry, load balancing, and OpenAI API format conversion.

## Features

### Core Proxy
- **Multi-Backend Support** - Multiple API URLs and Keys with group management
- **Auto Failover** - Automatic switching on failure with cooldown recovery
- **Load Balancing** - Round-robin rotation across backends
- **Retry Logic** - Configurable retry count with intelligent error handling
- **Empty Response Correction** - Auto-retry when server returns HTTP 200 with empty body
- **Request Timeout** - Configurable timeout (30-600s) for long Claude Code sessions

### API Conversion
- **OpenAI Format** - Translate Claude API format to OpenAI Chat Completions format
- **Streaming Support** - Full SSE streaming conversion with thinking block support
- **Model Mapping** - Map model names between Claude Code and backend providers
- **Local Token Count** - Fast local token estimation without API calls

### Claude Code Settings Manager
- **Read/Write** `~/.claude/settings.json` directly from the GUI
- **Mode Detection** - Automatically detect API vs Subscription mode
- **Environment Toggles** - Enable/disable features via checkboxes:
  - Disable Non-Essential Calls / Traffic
  - Disable Experimental Betas / Auto Updater / Auto Memory
  - Enable MCP CLI / Agent Teams / LSP Tools
- **Connection Config** - Edit Base URL, Auth Token, Timeouts
- **Settings Config** - Model, Fast Mode, Teammate Mode/Model, Permissions
- **Read-Only Detection** - Only writes when user explicitly clicks Save

### Tools
- **Conversation Browser** - Browse all Claude Code conversations from `~/.claude/projects/`
- **Export** - Export conversations to Markdown or JSON

### UI
- **macOS Native Style** - Dark/Light mode auto-detection
- **Colored Logs** - Terminal-style log view with color-coded messages
- **Live Stats** - Real-time success/error/correction counters

## Quick Start

### Requirements
- Qt 6.10.1+
- CMake 3.16+
- Xcode Command Line Tools (macOS)
- MinGW (Windows)

### Build (macOS)

```bash
./build.sh
```

Output:
- `build/ccb.app` - Universal Binary (Intel + Apple Silicon)
- `build/ccb-1.0-universal.dmg` - DMG installer (~28MB)

### Build (Windows)

```batch
build.bat
```

### Manual Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos
make -j$(sysctl -n hw.ncpu)
```

## Usage

1. Launch the app
2. Add backend URL(s) and API Key(s)
3. Configure port (default: 8080)
4. Click **Start**
5. Point Claude Code to `http://127.0.0.1:<port>`

### Claude Code Settings

Access via **Tools > Claude Code Settings** (`Ctrl+Shift+S`):
- Environment tab: Toggle feature flags, edit connection parameters
- Settings tab: Configure model, agent teams, permissions

The app auto-detects whether you're using API mode (has `ANTHROPIC_BASE_URL`) or Subscription mode.

## Configuration

### Proxy Config

Saved to: `~/Library/Application Support/ccb/config.json`

| Setting | Range | Default | Description |
|---------|-------|---------|-------------|
| Port | 1024-65535 | 8080 | Proxy listen port |
| Retry | 1-15 | 3 | Max retry attempts |
| Cooldown | 0-30s | 3s | Backend cooldown after failure |
| Timeout | 30-600s | 300s | Request timeout |
| Correction | on/off | on | Auto-retry on empty 200 response |

### Group Features

Each group can have:
- Multiple URLs and Keys
- Independent model mappings
- OpenAI format toggle
- Enable/disable individual backends

## Platform Support

| Platform | Architecture | Min Version |
|----------|-------------|-------------|
| macOS | x86_64 + arm64 (Universal) | 13.0 Ventura |
| Windows | x64 | Windows 10 |

## Project Structure

```
ccb/
├── main.cpp                      # Entry point
├── mainwindow.cpp/h              # Main window UI
├── backendpool.cpp/h             # Backend pool management
├── configmanager.cpp/h           # Proxy config (config.json)
├── claudesettingsmanager.cpp/h   # Claude settings (~/.claude/settings.json)
├── claudesettingsdialog.cpp/h    # Claude settings dialog UI
├── requesthandler.cpp/h          # HTTP request handling
├── proxyserver.cpp/h             # TCP proxy server
├── conversationbrowser.cpp/h     # Conversation browser
├── macosstylemanager.cpp/h       # Theme management
├── licensemanager.cpp/h          # License validation
├── licensedialog.cpp/h           # License dialog
├── logger.cpp/h                  # File logging
├── conversion/                   # API format conversion
│   ├── request_converter.cpp/h   # Claude -> OpenAI
│   ├── response_converter.cpp/h  # OpenAI -> Claude
│   └── streaming_converter.cpp/h # SSE streaming
├── build.sh                      # macOS build script
└── build.bat                     # Windows build script
```

## License

This project is licensed under the [GNU Affero General Public License v3.0 (AGPL-3.0)](LICENSE).

## Author

- **GitHub**: [uk0](https://github.com/uk0)
- **Blog**: [firsh.me](https://firsh.me)
