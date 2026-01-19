#!/bin/bash

# Claude Code Proxy - Build Script
# Builds Universal Binary DMG for macOS (Intel + Apple Silicon)

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_NAME="ccb"
VERSION="1.0"
QT_PATH="${QT_PATH:-/Users/firshme/Qt/6.10.1/macos}"
BUILD_DIR="build"
DMG_NAME="${PROJECT_NAME}-${VERSION}-universal.dmg"

# Print banner
echo -e "${BLUE}"
echo "============================================="
echo "  Claude Code Proxy - Build Script"
echo "  Version: ${VERSION}"
echo "============================================="
echo -e "${NC}"

# Check Qt installation
check_qt() {
    echo -e "${YELLOW}[1/6] Checking Qt installation...${NC}"

    if [ ! -d "$QT_PATH" ]; then
        echo -e "${RED}Error: Qt not found at $QT_PATH${NC}"
        echo "Please set QT_PATH environment variable or install Qt 6.10.1"
        echo "Example: export QT_PATH=/path/to/Qt/6.10.1/macos"
        exit 1
    fi

    if [ ! -f "$QT_PATH/bin/qmake" ]; then
        echo -e "${RED}Error: qmake not found in $QT_PATH/bin${NC}"
        exit 1
    fi

    QT_VERSION=$("$QT_PATH/bin/qmake" -query QT_VERSION)
    echo -e "${GREEN}✓ Found Qt $QT_VERSION at $QT_PATH${NC}"
}

# Check dependencies
check_dependencies() {
    echo -e "${YELLOW}[2/6] Checking dependencies...${NC}"

    # Check cmake
    if ! command -v cmake &> /dev/null; then
        echo -e "${RED}Error: cmake not found. Please install cmake.${NC}"
        exit 1
    fi
    CMAKE_VERSION=$(cmake --version | head -n1)
    echo -e "${GREEN}✓ $CMAKE_VERSION${NC}"

    # Check Xcode command line tools
    if ! command -v clang &> /dev/null; then
        echo -e "${RED}Error: Xcode command line tools not found.${NC}"
        echo "Please run: xcode-select --install"
        exit 1
    fi
    CLANG_VERSION=$(clang --version | head -n1)
    echo -e "${GREEN}✓ $CLANG_VERSION${NC}"

    # Check hdiutil (for DMG creation)
    if ! command -v hdiutil &> /dev/null; then
        echo -e "${RED}Error: hdiutil not found.${NC}"
        exit 1
    fi
    echo -e "${GREEN}✓ hdiutil available${NC}"
}

# Clean build directory
clean_build() {
    echo -e "${YELLOW}[3/6] Cleaning build directory...${NC}"

    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        echo -e "${GREEN}✓ Removed old build directory${NC}"
    fi

    mkdir -p "$BUILD_DIR"
    echo -e "${GREEN}✓ Created fresh build directory${NC}"
}

# Configure with CMake
configure() {
    echo -e "${YELLOW}[4/6] Configuring with CMake...${NC}"

    cd "$BUILD_DIR"

    cmake \
        -DCMAKE_PREFIX_PATH="$QT_PATH" \
        -DCMAKE_BUILD_TYPE=Release \
        ..

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ CMake configuration successful${NC}"
    else
        echo -e "${RED}Error: CMake configuration failed${NC}"
        exit 1
    fi

    cd ..
}

# Build the project
build() {
    echo -e "${YELLOW}[5/6] Building project...${NC}"

    cd "$BUILD_DIR"

    # Get number of CPU cores for parallel build
    CORES=$(sysctl -n hw.ncpu)
    echo "Building with $CORES parallel jobs..."

    cmake --build . --parallel "$CORES"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ Build successful${NC}"
    else
        echo -e "${RED}Error: Build failed${NC}"
        exit 1
    fi

    cd ..
}

# Deploy and create DMG
create_dmg() {
    echo -e "${YELLOW}[6/6] Creating DMG installer...${NC}"

    cd "$BUILD_DIR"

    # Deploy Qt frameworks and create DMG
    cmake --build . --target dmg

    if [ $? -eq 0 ] && [ -f "$DMG_NAME" ]; then
        echo -e "${GREEN}✓ DMG created successfully${NC}"
    else
        echo -e "${RED}Error: DMG creation failed${NC}"
        exit 1
    fi

    cd ..
}

# Verify the build
verify_build() {
    echo ""
    echo -e "${BLUE}=============================================${NC}"
    echo -e "${BLUE}  Build Verification${NC}"
    echo -e "${BLUE}=============================================${NC}"

    APP_PATH="$BUILD_DIR/${PROJECT_NAME}.app"
    DMG_PATH="$BUILD_DIR/$DMG_NAME"
    BINARY_PATH="$APP_PATH/Contents/MacOS/$PROJECT_NAME"

    # Check app bundle
    if [ -d "$APP_PATH" ]; then
        echo -e "${GREEN}✓ App bundle: $APP_PATH${NC}"
        APP_SIZE=$(du -sh "$APP_PATH" | cut -f1)
        echo "  Size: $APP_SIZE"
    else
        echo -e "${RED}✗ App bundle not found${NC}"
    fi

    # Check architectures
    if [ -f "$BINARY_PATH" ]; then
        ARCHS=$(lipo -info "$BINARY_PATH" 2>/dev/null | sed 's/.*are: //')
        echo -e "${GREEN}✓ Architectures: $ARCHS${NC}"
    fi

    # Check deployment target
    if [ -f "$BINARY_PATH" ]; then
        MINOS=$(otool -l "$BINARY_PATH" | grep -A 3 "LC_BUILD_VERSION" | grep "minos" | head -1 | awk '{print $2}')
        echo -e "${GREEN}✓ Minimum macOS: $MINOS${NC}"
    fi

    # Check DMG
    if [ -f "$DMG_PATH" ]; then
        DMG_SIZE=$(du -sh "$DMG_PATH" | cut -f1)
        echo -e "${GREEN}✓ DMG: $DMG_PATH${NC}"
        echo "  Size: $DMG_SIZE"
    else
        echo -e "${RED}✗ DMG not found${NC}"
    fi

    # Check Info.plist
    PLIST_PATH="$APP_PATH/Contents/Info.plist"
    if [ -f "$PLIST_PATH" ]; then
        MIN_VERSION=$(plutil -p "$PLIST_PATH" | grep LSMinimumSystemVersion | sed 's/.*=> "//' | sed 's/"//')
        echo -e "${GREEN}✓ LSMinimumSystemVersion: $MIN_VERSION${NC}"
    fi
}

# Print summary
print_summary() {
    echo ""
    echo -e "${BLUE}=============================================${NC}"
    echo -e "${GREEN}  Build Complete!${NC}"
    echo -e "${BLUE}=============================================${NC}"
    echo ""
    echo "Output files:"
    echo "  App:  $(pwd)/$BUILD_DIR/${PROJECT_NAME}.app"
    echo "  DMG:  $(pwd)/$BUILD_DIR/$DMG_NAME"
    echo ""
    echo "Supported platforms:"
    echo "  - macOS 13.0+ (Ventura, Sonoma, Sequoia)"
    echo "  - Intel Mac (x86_64)"
    echo "  - Apple Silicon (arm64)"
    echo ""
}

# Main execution
main() {
    # Get script directory
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"

    echo "Working directory: $SCRIPT_DIR"
    echo ""

    check_qt
    check_dependencies
    clean_build
    configure
    build
    create_dmg
    verify_build
    print_summary
}

# Parse command line arguments
case "${1:-}" in
    clean)
        echo "Cleaning build directory..."
        rm -rf "$BUILD_DIR"
        echo "Done."
        ;;
    configure)
        check_qt
        check_dependencies
        clean_build
        configure
        ;;
    build)
        cd build 2>/dev/null || { echo "Run './build.sh configure' first"; exit 1; }
        cmake --build . --parallel
        ;;
    dmg)
        cd build 2>/dev/null || { echo "Run './build.sh' first"; exit 1; }
        cmake --build . --target dmg
        ;;
    verify)
        verify_build
        ;;
    help|--help|-h)
        echo "Usage: $0 [command]"
        echo ""
        echo "Commands:"
        echo "  (none)     Full build (clean, configure, build, dmg)"
        echo "  clean      Remove build directory"
        echo "  configure  Configure CMake only"
        echo "  build      Build only (requires configure first)"
        echo "  dmg        Create DMG only (requires build first)"
        echo "  verify     Verify the build output"
        echo "  help       Show this help message"
        echo ""
        echo "Environment variables:"
        echo "  QT_PATH    Path to Qt installation (default: /Users/firshme/Qt/6.10.1/macos)"
        echo ""
        echo "Examples:"
        echo "  ./build.sh                    # Full build"
        echo "  ./build.sh clean              # Clean only"
        echo "  QT_PATH=/opt/Qt/6.8/macos ./build.sh  # Use different Qt"
        ;;
    *)
        main
        ;;
esac
