@echo off
echo === Zerodha Trading Bot ===
echo.

REM Check if executable exists
if not exist "build\Release\ZerodhaTradingBot.exe" (
    echo Error: Executable not found. Please run build.bat first.
    pause
    exit /b 1
)

REM Check if credential file exists
if not exist "Credential.csv" (
    echo Error: Credential.csv not found in current directory.
    echo Please copy your credential file to the project root.
    pause
    exit /b 1
)

REM Check if trade settings file exists
if not exist "TradeSettings.csv" (
    echo Error: TradeSettings.csv not found in current directory.
    echo Please copy your trade settings file to the project root.
    pause
    exit /b 1
)

echo Starting Zerodha Trading Bot...
echo.
echo This will:
echo 1. Load credentials and trade settings
echo 2. Login to Zerodha
echo 3. Fetch instrument data
echo 4. Get historical data for all symbols (10 days ago to today 15:15:00)
echo.

REM Run the executable
build\Release\ZerodhaTradingBot.exe

echo.
echo Program finished.
pause 