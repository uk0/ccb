cmake_minimum_required(VERSION 3.16)

# macOS deployment settings - MUST be set BEFORE project()
if(APPLE)
    # Minimum macOS version - Qt 6.10.1 requires macOS 13.0+
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version" FORCE)

    # Build Universal Binary (Intel + Apple Silicon)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures" FORCE)
endif()

project(ccb VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        backendpool.h
        backendpool.cpp
        configmanager.h
        configmanager.cpp
        requesthandler.h
        requesthandler.cpp
        proxyserver.h
        proxyserver.cpp
        logger.h
        logger.cpp
        licensemanager.h
        licensemanager.cpp
        licensedialog.h
        licensedialog.cpp
        macosstylemanager.h
        macosstylemanager.cpp
        # Conversion module - Claude/OpenAI format conversion
        conversion/constants.h
        conversion/types.h
        conversion/request_converter.h
        conversion/request_converter.cpp
        conversion/response_converter.h
        conversion/response_converter.cpp
        conversion/streaming_converter.h
        conversion/streaming_converter.cpp
        resources.qrc
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(ccb
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET ccb APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(ccb SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(ccb
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(ccb PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network)

# Platform-specific libraries for hardware identification
if(APPLE)
    # IOKit framework for getting hardware UUID
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(ccb PRIVATE ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.ccb)
endif()
set_target_properties(ccb PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_ICON_FILE AppIcon.icns
    MACOSX_BUNDLE_GUI_IDENTIFIER "me.firsh.ccb"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    WIN32_EXECUTABLE TRUE
)

# Sign with entitlements on macOS
if(APPLE)
    set(ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ccb.entitlements")
    add_custom_command(TARGET ccb POST_BUILD
        COMMAND codesign --force --sign - --entitlements "${ENTITLEMENTS_FILE}" "$<TARGET_BUNDLE_DIR:ccb>"
        COMMENT "Signing app bundle with entitlements"
    )
endif()

# Copy icon to bundle
set(APP_ICON_MACOS "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
target_sources(ccb PRIVATE ${APP_ICON_MACOS})

include(GNUInstallDirs)
install(TARGETS ccb
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(ccb)
endif()

# macOS deployment - bundle Qt frameworks
if(APPLE)
    # Find macdeployqt
    get_target_property(_qmake_executable Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin_dir}")

    # Custom target to deploy Qt frameworks
    add_custom_target(deploy
        COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:ccb>" -always-overwrite
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ccb>"
        COMMENT "Deploying Qt frameworks and signing..."
        DEPENDS ccb
    )

    # Custom target to create DMG
    add_custom_target(dmg
        COMMAND hdiutil create -volname "Claude Code Balance" -srcfolder "$<TARGET_BUNDLE_DIR:ccb>" -ov -format UDZO "${CMAKE_BINARY_DIR}/ccb-${PROJECT_VERSION}-universal.dmg"
        COMMENT "Creating DMG installer..."
        DEPENDS deploy
    )
endif()
