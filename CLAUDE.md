# CCB - Claude Code Proxy

## 项目概述

Qt6 透明代理应用，用于 Claude Code API 请求转发，支持多 URL/Key 自动切换、错误重试和负载均衡。

## 开发环境

### Qt 配置

- **Qt 版本**: 6.10.1 (官方安装器)
- **Qt 路径**: `/Users/firshme/Qt/6.10.1/macos`
- **架构**: Universal Binary (x86_64 + arm64)
- **最低 macOS**: 13.0 (Ventura) - Qt 6.10.1 要求

## 自动化构建

### build.sh 脚本

项目提供完整的自动化构建脚本：

```bash
# 完整构建（推荐）
./build.sh

# 其他命令
./build.sh clean      # 仅清理
./build.sh configure  # 仅配置
./build.sh build      # 仅编译
./build.sh dmg        # 仅创建DMG
./build.sh verify     # 验证构建结果
./build.sh help       # 显示帮助

# 使用自定义 Qt 路径
QT_PATH=/path/to/Qt/6.x/macos ./build.sh
```

**脚本功能：**
- 自动检测 Qt、CMake、Xcode
- 彩色输出，进度清晰
- 并行编译（自动使用全部 CPU 核心）
- 自动部署 Qt 框架
- 自动创建 DMG 安装包
- 构建验证（架构、最低系统版本、文件大小）

### Windows 构建 (build.bat)

```batch
:: 完整构建（推荐）
build.bat

:: 其他命令
build.bat clean      :: 仅清理
build.bat configure  :: 仅配置
build.bat build      :: 仅编译
build.bat package    :: 部署 Qt 框架
build.bat installer  :: 创建安装包
build.bat help       :: 显示帮助

:: 使用自定义 Qt 路径
set QT_PATH=D:\Qt\6.10.1\mingw_64
set QT_TOOLS=D:\Qt\Tools\mingw1310_64
build.bat
```

**Windows 环境要求：**
- Qt 6.x for Windows (MinGW 64-bit)
- MinGW (Qt Tools 目录中自带)
- CMake (Qt Tools 目录中自带或系统安装)
- NSIS (可选，用于创建安装程序)

**默认路径：**
- Qt: `C:\Qt\6.10.1\mingw_64`
- MinGW: `C:\Qt\Tools\mingw1310_64`

**输出文件：**
- `build\ccb.exe` - 可执行文件
- `build\deploy\` - 包含所有依赖的目录
- `build\ccb-1.0-setup.exe` - 安装程序 (需要 NSIS)
- `build\ccb-1.0-windows-x64.zip` - ZIP 发布包

### 手动构建命令 (macOS)

```bash
# 完整构建流程（Universal Binary）
mkdir -p build && cd build
rm -rf CMakeCache.txt CMakeFiles  # 清理旧缓存
cmake .. -DCMAKE_PREFIX_PATH=/Users/firshme/Qt/6.10.1/macos \
         -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
make -j4

# 部署 Qt 框架到 app bundle
make deploy

# 创建 DMG 安装包
make dmg
```

### 快速开发构建（仅当前架构）

```bash
cd build
cmake .. -DCMAKE_PREFIX_PATH=/Users/firshme/Qt/6.10.1/macos
make -j4
```

## 项目结构

```
ccb/
├── main.cpp                 # 入口，License 检查
├── mainwindow.cpp/h         # 主窗口 UI
├── mainwindow.ui            # Qt Designer UI 文件
├── backendpool.cpp/h        # 后端 URL/Key 池管理
├── configmanager.cpp/h      # 配置管理
├── requesthandler.cpp/h     # HTTP 请求处理
├── proxyserver.cpp/h        # 代理服务器
├── logger.cpp/h             # 日志系统
├── licensemanager.cpp/h     # License 验证
├── licensedialog.cpp/h      # License 激活对话框
├── resources.qrc            # 资源文件
├── CMakeLists.txt           # CMake 构建配置
├── Info.plist.in            # macOS 应用信息
├── ccb.entitlements         # macOS 权限配置
├── AppIcon.icns             # 应用图标
├── build.sh                 # macOS 自动化构建脚本
├── build.bat                # Windows 自动化构建脚本
├── CLAUDE.md                # 项目文档
└── tools/
    └── generate_license.py  # License 生成脚本
```

## 配置选项

### UI 配置项

| 配置项 | 范围 | 默认值 | 说明 |
|--------|------|--------|------|
| Port | 1-65535 | 8080 | 代理服务器监听端口 |
| Retry | 1-15 | 3 | 请求失败重试次数 |
| Cooldown(s) | 0-30 | 3 | 后端失败后冷却时间（秒）|
| Timeout(s) | 30-600 | 300 | 请求超时时间（秒），适配 Claude Code 长请求 |
| Correction | on/off | on | 空响应纠错：当服务器返回 200 但响应为空时自动重试 |

### 配置文件

配置保存在: `~/Library/Application Support/ccb/config.json`

```json
{
    "listenPort": 8080,
    "retryCount": 3,
    "cooldownSeconds": 3,
    "timeoutSeconds": 300,
    "correctionEnabled": true,
    "apiUrls": [...],
    "apiKeys": [...]
}
```

## License 系统

### 工作原理

1. **机器码生成**: SHA256(MAC地址 + 主机名 + machineUniqueId + 盐值)
   - 使用 `QSysInfo::machineUniqueId()` 确保跨系统版本稳定
   - 盐值: `CCB_SALT_UK0_2024_V2`
2. **License 结构**: `机器码哈希(8字节) + 过期日期(4字节) + 签名(4字节)`
3. **编码方式**: 自定义 Base32（32字符: `ABCDEFGHJKMNPQRSTUVWXYZ23456789W`）

### 生成 License

```bash
cd tools
python generate_license.py "机器码" "2025-12-31"
```

### 密钥配置（licensemanager.cpp）

```cpp
const QByteArray LicenseManager::SECRET_KEY = "CCB_LICENSE_KEY_2024_FIRSH_ME";
const QByteArray LicenseManager::STORAGE_KEY = "CCB_STORAGE_ENC_KEY_UK0";
```

## macOS 部署配置

### CMakeLists.txt 关键设置

```cmake
# 重要：必须在 project() 之前设置
if(APPLE)
    # 最低 macOS 版本 (Qt 6.10.1 需要 13.0+)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)

    # Universal Binary（需要 Qt Universal）
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
endif()

project(ccb VERSION 1.0 LANGUAGES CXX)
```

### Info.plist.in

- `LSMinimumSystemVersion`: 13.0 (Ventura)
- `CFBundleIdentifier`: me.firsh.ccb

## UI 组件说明

### License Dialog (licensedialog.cpp)

- **机器码显示**: 黑底白字 (`#1a1a1a` / `#ffffff`)
- **Copy 按钮**: 蓝色 (`#2196F3`)，60x32px
- **Activate 按钮**: 绿色 (`#4CAF50`)
- **Exit/Continue 按钮**: 深灰 (`#424242`)，白色文字

### About Dialog (mainwindow.cpp::showAbout)

- 显示 License 状态（激活/未激活）
- 显示过期日期和剩余天数
- Delete License 按钮（红色 `#f44336`）

## 已知问题和解决方案

### Base32 编码问题

**问题**: 原始 alphabet 只有 31 个字符，导致 IndexError
**解决**: 添加 'W' 使其达到 32 个字符

```python
# Python (generate_license.py)
BASE32_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789W"
```

```cpp
// C++ (licensemanager.cpp)
static const char alphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789W";
```

### UI 堆叠问题

**问题**: License Dialog 元素重叠
**解决**: 为所有 GroupBox 设置固定高度

```cpp
machineGroup->setFixedHeight(100);
licenseGroup->setFixedHeight(120);
statusGroup->setFixedHeight(80);
```

### macdeployqt rpath 警告

**问题**: 部署时出现 rpath 警告
**原因**: 可选 Qt 模块未安装
**影响**: 不影响核心功能，可忽略

### 机器码跨系统版本不一致

**问题**: 使用 `QSysInfo::prettyProductName()` 生成机器码，不同 macOS 版本返回不同值
**解决**: 改用 `QSysInfo::machineUniqueId()` 生成稳定的硬件唯一标识

```cpp
// 旧方法（有问题）
hwInfo.append(QSysInfo::prettyProductName().toUtf8());  // "macOS 15.6" vs "macOS 13.0"

// 新方法（稳定）
hwInfo.append(QSysInfo::machineUniqueId());  // 硬件唯一ID，不随系统更新变化
```

**注意**: 更新后旧 License 失效，需用新机器码重新生成

### CMAKE_OSX_DEPLOYMENT_TARGET 不生效

**问题**: 设置部署目标后二进制仍显示错误的 minos 版本
**原因**: `CMAKE_OSX_DEPLOYMENT_TARGET` 必须在 `project()` 之前设置
**解决**:

```cmake
# 正确顺序
cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)  # 在 project() 之前
project(ccb VERSION 1.0 LANGUAGES CXX)
```

## 构建输出

### macOS

| 文件 | 大小 | 说明 |
|------|------|------|
| `ccb.app` | ~56 MB | Universal Binary 应用包（含 Qt 框架）|
| `ccb-1.0-universal.dmg` | ~27 MB | DMG 安装镜像 |

**支持的系统：**
- macOS 13 Ventura
- macOS 14 Sonoma
- macOS 15 Sequoia
- Intel Mac (x86_64) 和 Apple Silicon (arm64)

### Windows

| 文件 | 大小 | 说明 |
|------|------|------|
| `ccb.exe` | ~1 MB | 可执行文件 |
| `deploy/` | ~50 MB | 含 Qt 依赖的完整目录 |
| `ccb-1.0-setup.exe` | ~25 MB | NSIS 安装程序 |
| `ccb-1.0-windows-x64.zip` | ~25 MB | ZIP 发布包 |

**支持的系统：**
- Windows 10 x64
- Windows 11 x64

**安装程序功能 (NSIS)：**
- 自动安装到 Program Files
- 创建开始菜单快捷方式
- 创建桌面快捷方式
- 支持卸载

## 版本历史

### v1.0
- 基础代理功能
- 多 URL/Key 支持
- 自动故障转移
- 离线 License 系统
- 可配置超时时间 (30-600秒)
- 空响应纠错 (Correction) 功能
- macOS 13+ 支持 (Qt 6.10.1 要求)
- Windows 10/11 x64 支持
- Universal Binary (Intel + Apple Silicon)
- 自动化构建脚本 (build.sh / build.bat)
- 跨系统版本稳定的机器码生成 (machineUniqueId)

## 作者

- **Author**: uk0
- **GitHub**: https://github.com/uk0
- **Blog**: https://firsh.me
