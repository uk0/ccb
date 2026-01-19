@echo off
setlocal enabledelayedexpansion

:: Claude Code Proxy - Windows Build Script (MinGW)
:: Builds x64 executable with Qt MinGW toolchain

:: Configuration
set "PROJECT_NAME=ccb"
set "VERSION=1.0"
set "BUILD_DIR=build"
set "BUILD_TYPE=Release"

:: Qt MinGW path - adjust this for your system
if "%QT_PATH%"=="" (
    set "QT_PATH=C:\Qt\6.10.1\mingw_64"
)

:: Qt Tools path (contains MinGW)
if "%QT_TOOLS%"=="" (
    set "QT_TOOLS=C:\Qt\Tools\mingw1310_64"
)

echo.
echo =============================================
echo   Claude Code Proxy - Windows Build Script
echo   Version: %VERSION% [MinGW]
echo =============================================
echo.

:: Parse command line arguments
if "%1"=="clean" goto :clean
if "%1"=="configure" goto :configure_only
if "%1"=="build" goto :build_only
if "%1"=="package" goto :package_only
if "%1"=="installer" goto :installer_only
if "%1"=="help" goto :help
if "%1"=="--help" goto :help
if "%1"=="-h" goto :help
goto :main

:help
echo Usage: build.bat [command]
echo.
echo Commands:
echo   (none)     Full build (clean, configure, build, package, installer)
echo   clean      Remove build directory
echo   configure  Configure CMake only
echo   build      Build only (requires configure first)
echo   package    Deploy Qt frameworks only
echo   installer  Create installer only (requires package first)
echo   help       Show this help message
echo.
echo Environment variables:
echo   QT_PATH    Path to Qt MinGW (default: C:\Qt\6.10.1\mingw_64)
echo   QT_TOOLS   Path to MinGW tools (default: C:\Qt\Tools\mingw1310_64)
echo.
echo Examples:
echo   build.bat
echo   build.bat clean
echo   set QT_PATH=D:\Qt\6.8.0\mingw_64 ^&^& build.bat
goto :eof

:clean
echo [*] Cleaning build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    echo [OK] Done.
) else (
    echo [--] Nothing to clean.
)
goto :eof

:check_dependencies
echo [1/6] Checking dependencies...

:: Check Qt
if not exist "%QT_PATH%\bin\qmake.exe" (
    echo [ERROR] Qt not found at %QT_PATH%
    echo         Please set QT_PATH environment variable
    echo         Example: set QT_PATH=C:\Qt\6.10.1\mingw_64
    exit /b 1
)
echo   [OK] Qt: %QT_PATH%

:: Check MinGW
if not exist "%QT_TOOLS%\bin\gcc.exe" (
    echo   [WARN] MinGW not found at %QT_TOOLS%
    echo   [WARN] Trying to find MinGW in Qt installation...
    
    :: Try common paths
    if exist "C:\Qt\Tools\mingw1310_64\bin\gcc.exe" (
        set "QT_TOOLS=C:\Qt\Tools\mingw1310_64"
    ) else if exist "C:\Qt\Tools\mingw1120_64\bin\gcc.exe" (
        set "QT_TOOLS=C:\Qt\Tools\mingw1120_64"
    ) else if exist "C:\Qt\Tools\mingw810_64\bin\gcc.exe" (
        set "QT_TOOLS=C:\Qt\Tools\mingw810_64"
    ) else (
        echo [ERROR] MinGW not found
        echo         Please install MinGW via Qt Maintenance Tool
        exit /b 1
    )
)
echo   [OK] MinGW: %QT_TOOLS%

:: Check CMake (use Qt's cmake if system cmake not found)
set "CMAKE_EXE=cmake"
where cmake >nul 2>&1
if errorlevel 1 (
    if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" (
        set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
        echo   [OK] CMake: C:\Qt\Tools\CMake_64
    ) else (
        echo [ERROR] CMake not found
        echo         Please install CMake or use Qt Maintenance Tool
        exit /b 1
    )
) else (
    echo   [OK] CMake: Found in PATH
)

:: Check for NSIS (optional, for installer)
set "NSIS_EXE="
if exist "C:\Program Files (x86)\NSIS\makensis.exe" (
    set "NSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe"
    echo   [OK] NSIS: Found (installer will be created)
) else if exist "C:\Program Files\NSIS\makensis.exe" (
    set "NSIS_EXE=C:\Program Files\NSIS\makensis.exe"
    echo   [OK] NSIS: Found (installer will be created)
) else (
    echo   [--] NSIS: Not found (will create ZIP only)
)

echo   [OK] All dependencies checked
goto :eof

:setup_environment
echo [2/6] Setting up build environment...

:: Add MinGW and Qt to PATH
set "PATH=%QT_TOOLS%\bin;%QT_PATH%\bin;%PATH%"

echo   [OK] Environment configured
goto :eof

:configure
echo [3/6] Configuring with CMake...

if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

%CMAKE_EXE% -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%" ^
    -DCMAKE_C_COMPILER="%QT_TOOLS%\bin\gcc.exe" ^
    -DCMAKE_CXX_COMPILER="%QT_TOOLS%\bin\g++.exe" ^
    -DCMAKE_MAKE_PROGRAM="%QT_TOOLS%\bin\mingw32-make.exe" ^
    ..

if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    exit /b 1
)

cd ..
echo   [OK] CMake configuration successful
goto :eof

:build
echo [4/6] Building project...

cd "%BUILD_DIR%"

:: Get number of processors for parallel build
set /a JOBS=%NUMBER_OF_PROCESSORS%
if %JOBS% LSS 1 set JOBS=4

echo   Building with %JOBS% parallel jobs...
mingw32-make -j%JOBS%

if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    exit /b 1
)

cd ..
echo   [OK] Build successful
goto :eof

:package
echo [5/6] Deploying Qt frameworks...

set "DEPLOY_DIR=%BUILD_DIR%\deploy"
set "EXE_PATH=%BUILD_DIR%\%PROJECT_NAME%.exe"

:: Check if executable exists
if not exist "%EXE_PATH%" (
    echo [ERROR] Executable not found at %EXE_PATH%
    exit /b 1
)

:: Create deploy directory
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"

:: Copy executable
copy "%EXE_PATH%" "%DEPLOY_DIR%\" >nul
echo   Copied executable

:: Run windeployqt
echo   Running windeployqt...
"%QT_PATH%\bin\windeployqt.exe" ^
    --release ^
    --no-translations ^
    --no-system-d3d-compiler ^
    --no-opengl-sw ^
    --compiler-runtime ^
    "%DEPLOY_DIR%\%PROJECT_NAME%.exe"

if errorlevel 1 (
    echo [ERROR] windeployqt failed
    exit /b 1
)

:: Copy MinGW runtime DLLs
echo   Copying MinGW runtime...
if exist "%QT_TOOLS%\bin\libgcc_s_seh-1.dll" copy "%QT_TOOLS%\bin\libgcc_s_seh-1.dll" "%DEPLOY_DIR%\" >nul
if exist "%QT_TOOLS%\bin\libstdc++-6.dll" copy "%QT_TOOLS%\bin\libstdc++-6.dll" "%DEPLOY_DIR%\" >nul
if exist "%QT_TOOLS%\bin\libwinpthread-1.dll" copy "%QT_TOOLS%\bin\libwinpthread-1.dll" "%DEPLOY_DIR%\" >nul

echo   [OK] Qt deployment successful
goto :eof

:installer
echo [6/6] Creating installer...

set "DEPLOY_DIR=%BUILD_DIR%\deploy"
set "ZIP_NAME=%PROJECT_NAME%-%VERSION%-windows-x64.zip"
set "INSTALLER_NAME=%PROJECT_NAME%-%VERSION%-setup.exe"

:: Check deploy directory exists
if not exist "%DEPLOY_DIR%\%PROJECT_NAME%.exe" (
    echo [ERROR] Deploy directory not ready. Run 'build.bat package' first.
    exit /b 1
)

:: Try NSIS installer first
if defined NSIS_EXE (
    echo   Creating NSIS installer script...
    call :create_nsis_script
    
    echo   Running NSIS...
    "%NSIS_EXE%" /V2 "%BUILD_DIR%\installer.nsi"
    
    if exist "%BUILD_DIR%\%INSTALLER_NAME%" (
        echo   [OK] Installer created: %BUILD_DIR%\%INSTALLER_NAME%
    ) else (
        echo   [WARN] NSIS failed, creating ZIP instead...
        goto :create_zip
    )
) else (
    goto :create_zip
)
goto :installer_done

:create_zip
echo   Creating ZIP package...
cd "%DEPLOY_DIR%"
if exist "..\%ZIP_NAME%" del "..\%ZIP_NAME%"

:: Use PowerShell to create ZIP
powershell -Command "Compress-Archive -Path '*' -DestinationPath '..\%ZIP_NAME%' -Force"

cd ..\..

if exist "%BUILD_DIR%\%ZIP_NAME%" (
    echo   [OK] ZIP created: %BUILD_DIR%\%ZIP_NAME%
) else (
    echo [ERROR] Failed to create ZIP
    exit /b 1
)
goto :installer_done

:installer_done
goto :eof

:create_nsis_script
:: Create NSIS installer script
(
echo !include "MUI2.nsh"
echo.
echo Name "Claude Code Proxy"
echo OutFile "%INSTALLER_NAME%"
echo InstallDir "$PROGRAMFILES64\Claude Code Proxy"
echo InstallDirRegKey HKLM "Software\ClaudeCodeProxy" "InstallDir"
echo RequestExecutionLevel admin
echo.
echo !define MUI_ABORTWARNING
echo.
echo !insertmacro MUI_PAGE_WELCOME
echo !insertmacro MUI_PAGE_DIRECTORY
echo !insertmacro MUI_PAGE_INSTFILES
echo !insertmacro MUI_PAGE_FINISH
echo.
echo !insertmacro MUI_UNPAGE_CONFIRM
echo !insertmacro MUI_UNPAGE_INSTFILES
echo.
echo !insertmacro MUI_LANGUAGE "English"
echo !insertmacro MUI_LANGUAGE "SimpChinese"
echo.
echo Section "Install"
echo   SetOutPath "$INSTDIR"
echo   File /r "%DEPLOY_DIR%\*.*"
echo.
echo   ; Create shortcuts
echo   CreateDirectory "$SMPROGRAMS\Claude Code Proxy"
echo   CreateShortcut "$SMPROGRAMS\Claude Code Proxy\Claude Code Proxy.lnk" "$INSTDIR\%PROJECT_NAME%.exe"
echo   CreateShortcut "$SMPROGRAMS\Claude Code Proxy\Uninstall.lnk" "$INSTDIR\uninstall.exe"
echo   CreateShortcut "$DESKTOP\Claude Code Proxy.lnk" "$INSTDIR\%PROJECT_NAME%.exe"
echo.
echo   ; Write registry
echo   WriteRegStr HKLM "Software\ClaudeCodeProxy" "InstallDir" "$INSTDIR"
echo   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeCodeProxy" "DisplayName" "Claude Code Proxy"
echo   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeCodeProxy" "UninstallString" "$INSTDIR\uninstall.exe"
echo   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeCodeProxy" "DisplayVersion" "%VERSION%"
echo   WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeCodeProxy" "Publisher" "uk0"
echo.
echo   ; Create uninstaller
echo   WriteUninstaller "$INSTDIR\uninstall.exe"
echo SectionEnd
echo.
echo Section "Uninstall"
echo   ; Remove files
echo   RMDir /r "$INSTDIR"
echo.
echo   ; Remove shortcuts
echo   Delete "$SMPROGRAMS\Claude Code Proxy\*.lnk"
echo   RMDir "$SMPROGRAMS\Claude Code Proxy"
echo   Delete "$DESKTOP\Claude Code Proxy.lnk"
echo.
echo   ; Remove registry
echo   DeleteRegKey HKLM "Software\ClaudeCodeProxy"
echo   DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ClaudeCodeProxy"
echo SectionEnd
) > "%BUILD_DIR%\installer.nsi"
goto :eof

:verify
echo.
echo =============================================
echo   Build Verification
echo =============================================

set "EXE_PATH=%BUILD_DIR%\%PROJECT_NAME%.exe"
set "DEPLOY_DIR=%BUILD_DIR%\deploy"
set "ZIP_PATH=%BUILD_DIR%\%PROJECT_NAME%-%VERSION%-windows-x64.zip"
set "INSTALLER_PATH=%BUILD_DIR%\%PROJECT_NAME%-%VERSION%-setup.exe"

if exist "%EXE_PATH%" (
    echo   [OK] Executable: %EXE_PATH%
) else (
    echo   [--] Executable: Not found
)

if exist "%DEPLOY_DIR%\%PROJECT_NAME%.exe" (
    echo   [OK] Deploy dir: %DEPLOY_DIR%
) else (
    echo   [--] Deploy dir: Not ready
)

if exist "%INSTALLER_PATH%" (
    echo   [OK] Installer: %INSTALLER_PATH%
)

if exist "%ZIP_PATH%" (
    echo   [OK] ZIP: %ZIP_PATH%
)

goto :eof

:summary
echo.
echo =============================================
echo   Build Complete!
echo =============================================
echo.
echo Output files:
echo   Executable: %BUILD_DIR%\%PROJECT_NAME%.exe
echo   Deploy dir: %BUILD_DIR%\deploy\
if exist "%BUILD_DIR%\%PROJECT_NAME%-%VERSION%-setup.exe" (
    echo   Installer:  %BUILD_DIR%\%PROJECT_NAME%-%VERSION%-setup.exe
)
if exist "%BUILD_DIR%\%PROJECT_NAME%-%VERSION%-windows-x64.zip" (
    echo   ZIP:        %BUILD_DIR%\%PROJECT_NAME%-%VERSION%-windows-x64.zip
)
echo.
echo Supported: Windows 10/11 x64
echo.
goto :eof

:configure_only
call :check_dependencies
if errorlevel 1 exit /b 1
call :setup_environment
call :configure
goto :eof

:build_only
if not exist "%BUILD_DIR%\Makefile" (
    echo [ERROR] Run 'build.bat configure' first
    exit /b 1
)
call :setup_environment
call :build
goto :eof

:package_only
call :setup_environment
call :package
goto :eof

:installer_only
call :setup_environment
call :installer
goto :eof

:main
call :check_dependencies
if errorlevel 1 exit /b 1
call :setup_environment
call :configure
if errorlevel 1 exit /b 1
call :build
if errorlevel 1 exit /b 1
call :package
if errorlevel 1 exit /b 1
call :installer
call :verify
call :summary

endlocal
