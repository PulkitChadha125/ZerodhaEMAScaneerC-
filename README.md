# Zerodha Trading Bot

A C++ application for integrating with Zerodha's trading API. This bot provides functionality to authenticate with Zerodha and perform trading operations.

## Features

- **Credential Management**: Loads API credentials from CSV files
- **Authentication**: Implements Zerodha's login flow with request token generation
- **Session Management**: Handles access tokens and user sessions
- **HTTP Client**: Uses CPR library for making API requests

## Prerequisites

- CMake 3.15 or higher
- C++17 compatible compiler
- vcpkg (for dependency management)
- OpenSSL development libraries

## Dependencies

The following dependencies are managed through vcpkg:

- `nlohmann-json`: JSON parsing and manipulation
- `cpr`: HTTP client library
- `openssl`: Cryptographic functions for checksum generation

## Building the Project

1. **Clone and setup vcpkg** (if not already done):
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   ./bootstrap-vcpkg.sh  # On Windows: .\bootstrap-vcpkg.bat
   ```

2. **Install dependencies**:
   ```bash
   ./vcpkg install nlohmann-json cpr openssl
   ```

3. **Build the project**:
   ```bash
   mkdir build
   cd build
   cmake .. -DCMAKE_TOOLCHAIN_FILE=../../vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=../../vcpkg/installed/x64-windows
   cmake --build .
   ```

## Configuration

### Credentials File

Create a `Credential.csv` file in the project root with your Zerodha API credentials:

```csv
API_KEY,bfjw1om19ga3ho51
API_SECRET,8h0h3o4dihbzw5sinw7tcr5b85sey6lo
```

**Important**: Keep your API credentials secure and never commit them to version control.

## Usage

1. **Run the application**:
   ```bash
   ./ZerodhaTradingBot  # On Windows: ZerodhaTradingBot.exe
   ```

2. **Login Process**:
   - The application will load your credentials from `Credential.csv`
   - It will generate a login URL and display it
   - Open the URL in your browser and complete the login
   - Copy the request token from the browser
   - Paste the request token when prompted by the application

3. **Authentication Success**:
   - Upon successful authentication, you'll receive an access token
   - The application will display your User ID and a truncated access token
   - You're now ready to make API calls to Zerodha

## API Flow

1. **Credential Loading**: Reads API key and secret from CSV file
2. **Login URL Generation**: Creates a login URL with proper checksum
3. **User Authentication**: User completes login in browser
4. **Request Token**: User provides the request token from browser
5. **Session Token**: Application exchanges request token for access token
6. **API Access**: Application can now make authenticated API calls

## Security Notes

- API credentials are sensitive information
- Never share your API key or secret
- Use environment variables or secure credential storage in production
- The access token has a limited lifespan and needs to be refreshed

## Error Handling

The application includes comprehensive error handling for:
- Missing or invalid credentials
- Network connectivity issues
- Authentication failures
- API response parsing errors

## Next Steps

This implementation provides the foundation for Zerodha integration. Future enhancements could include:
- Market data streaming
- Order placement and management
- Portfolio tracking
- Automated trading strategies
- WebSocket connections for real-time data

## License

This project is for educational purposes. Please ensure compliance with Zerodha's API terms of service. 