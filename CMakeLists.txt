cmake_minimum_required(VERSION 3.15)
project(ZerodhaTradingBot)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_WIN32_WINNT=0x0601)  # Windows 7 or later
endif()

# Find required packages (let vcpkg toolchain handle paths)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)

# Add source files
set(SOURCES
    src/main.cpp
    src/zerodha_client.cpp
    src/csv_parser.cpp
)

# Add header files
set(HEADERS
    include/zerodha_client.h
    include/csv_parser.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    cpr::cpr
)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Copy required files to output directory
file(COPY ${CMAKE_SOURCE_DIR}/Credential.csv DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
file(COPY ${CMAKE_SOURCE_DIR}/TradeSettings.csv DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})