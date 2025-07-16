@echo off
echo ========================================
echo Building Zerodha Trading Bot
echo ========================================

REM Check if build directory exists, if not create it
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

REM Navigate to build directory
cd build

REM Configure with CMake
echo Configuring project with CMake...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/cpp/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows

REM Check if CMake configuration was successful
if %ERRORLEVEL% NEQ 0 (
    echo Error: CMake configuration failed!
    pause
    exit /b 1
)

REM Build the project
echo Building project...
cmake --build . --config Release

REM Check if build was successful
if %ERRORLEVEL% NEQ 0 (
    echo Error: Build failed!
    pause
    exit /b 1
)

REM Copy CSV files to Release directory
echo Copying configuration files...
if exist "bin\*.csv" (
    copy "bin\*.csv" "Release\" >nul
    echo Configuration files copied successfully.
) else (
    echo Warning: No CSV files found in bin directory.
)

echo ========================================
echo Build completed successfully!
echo Executable: build\Release\ZerodhaTradingBot.exe
echo ========================================
pause 