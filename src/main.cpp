#include "zerodha_client.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <chrono>
#include <iomanip>

#include <sstream>
#include <thread> // Added for std::this_thread::sleep_for

// Helper function to format date as yyyy-mm-dd hh:mm:ss
std::string formatDate(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

int main() {
    std::cout << "=== Zerodha Trading Bot ===" << std::endl;
    
    ZerodhaClient client;
    
    // Load credentials
    if (!client.loadCredentials("Credential.csv")) {
        std::cerr << "Failed to load credentials. Exiting." << std::endl;
        return 1;
    }
    
    // Load trade settings
    if (!client.loadTradeSettings("TradeSettings.csv")) {
        std::cerr << "Failed to load trade settings. Exiting." << std::endl;
        return 1;
    }
    
    std::cout << "\nStarting login process..." << std::endl;
    
    // Login to Zerodha
    if (!client.login()) {
        std::cerr << "Login failed. Exiting." << std::endl;
        return 1;
    }
    
    std::cout << "\nLogin successful! Fetching instruments..." << std::endl;
    
    // Step 1: Fetch instruments from Zerodha API
    if (!client.fetchInstruments()) {
        std::cerr << "Failed to fetch instruments. Exiting." << std::endl;
        return 1;
    }
    
    std::cout << "\nInstruments fetched successfully!" << std::endl;
    
    // Step 2: Save instruments to CSV file
    std::cout << "\nSaving instruments to CSV file..." << std::endl;
    if (!client.saveInstrumentsToCSV("instruments.csv")) {
        std::cerr << "Failed to save instruments to CSV. Exiting." << std::endl;
        return 1;
    }
    
    // Step 3: Load instruments from CSV (for demonstration)
    std::cout << "\nLoading instruments from CSV file..." << std::endl;
    if (!client.loadInstrumentsFromCSV("instruments.csv")) {
        std::cerr << "Failed to load instruments from CSV. Exiting." << std::endl;
        return 1;
    }
    
    // Step 4: Match symbols from trade settings with instruments
    std::cout << "\nMatching symbols from trade settings with instruments..." << std::endl;
    std::vector<std::string> matched_symbols = client.getMatchedSymbols();
    
    if (matched_symbols.empty()) {
        std::cerr << "No symbols matched. Exiting." << std::endl;
        return 1;
    }
    
    // Step 5: Fetch historical data for matched symbols
    std::cout << "\n=== Fetching Historical Data for Matched Symbols ===" << std::endl;
    
    // Calculate dynamic dates
    auto now = std::chrono::system_clock::now();
    auto ten_days_ago = now - std::chrono::hours(24 * 10); // 10 days ago
    
    // Format dates
    std::string from_date = formatDate(ten_days_ago);
    
    // Set to_date to today at 15:15:00
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    tm.tm_hour = 15;
    tm.tm_min = 15;
    tm.tm_sec = 0;
    std::string to_date = formatDate(std::chrono::system_clock::from_time_t(std::mktime(&tm)));
    
    std::cout << "Date range: " << from_date << " to " << to_date << std::endl;
    std::cout << "Fetching historical data for " << matched_symbols.size() << " matched symbols..." << std::endl;
    
    int success_count = 0;
    int total_count = 0;
    
    for (const auto& symbol : matched_symbols) {
        total_count++;
        std::cout << "[" << total_count << "/" << matched_symbols.size() << "] Processing " << symbol << "... ";
        
        // Get timeframe and EMA period from trade settings
        std::string timeframe = "5minute"; // Default
        int ema_period = 20; // Default
        for (const auto& setting : client.getTradeSettings()) {
            if (setting.symbol == symbol) {
                timeframe = setting.timeframe;
                ema_period = setting.ema_period;
                break;
            }
        }
        
        std::vector<CandleData> candles = client.getHistoricalData(symbol, timeframe, from_date, to_date);
        
        if (!candles.empty()) {
            std::cout << "✓ " << candles.size() << " candles";
            
            // Extract close prices for EMA calculation
            std::vector<double> close_prices;
            for (const auto& candle : candles) {
                close_prices.push_back(candle.close);
            }
            
            // Calculate EMA
            std::vector<double> ema_values = client.calculateEMA(close_prices, ema_period);
            std::cout << ", EMA(" << ema_period << ") calculated";
            
            // Save to CSV just comment it for not saving data to csv
            if (client.saveInstrumentDataToCSV(symbol, candles, ema_values)) {
                std::cout << ", saved to CSV";
            }
            
            std::cout << std::endl;
            success_count++;
        } else {
            std::cout << "✗ No data" << std::endl;
        }
        
        // Add a small delay to avoid rate limiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "\n=== Historical Data Fetch Complete ===" << std::endl;
    std::cout << "Successfully fetched data for " << success_count << " out of " << total_count << " symbols" << std::endl;
    
    // Start the trading loop
    std::cout << "\n=== Starting Trading Loop ===" << std::endl;
    std::cout << "Press Ctrl+C to stop the trading loop" << std::endl;
    
    client.runTradingLoop();
    
    return 0;
} 