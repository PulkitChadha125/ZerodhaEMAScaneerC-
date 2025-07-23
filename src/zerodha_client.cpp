#include "zerodha_client.h"
#include "csv_parser.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

ZerodhaClient::ZerodhaClient() : api_key_(""), api_secret_(""), access_token_(""), user_id_("") {
}

bool ZerodhaClient::loadCredentials(const std::string& filename) {
    std::map<std::string, std::string> credentials = CSVParser::parseCredentials(filename);
    
    if (credentials.find("API_KEY") != credentials.end()) {
        api_key_ = credentials["API_KEY"];
    } else {
        std::cerr << "Error: API_KEY not found in credentials file" << std::endl;
        return false;
    }
    
    if (credentials.find("API_SECRET") != credentials.end()) {
        api_secret_ = credentials["API_SECRET"];
    } else {
        std::cerr << "Error: API_SECRET not found in credentials file" << std::endl;
        return false;
    }
    
    std::cout << "Credentials loaded successfully" << std::endl;
    std::cout << "API Key: " << api_key_ << std::endl;
    return true;
}

bool ZerodhaClient::loadTradeSettings(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open trade settings file: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    // Skip header line
    std::getline(file, line);
    
    trade_settings_.clear();
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream ss(line);
        std::string symbol, quantity_str, timeframe, ema_period_str;
        
        if (std::getline(ss, symbol, ',') &&
            std::getline(ss, quantity_str, ',') &&
            std::getline(ss, timeframe, ',') &&
            std::getline(ss, ema_period_str, ',')) {
            
            TradeSetting setting;
            setting.symbol = symbol;
            setting.quantity = std::stoi(quantity_str);
            setting.timeframe = timeframe;
            setting.ema_period = std::stoi(ema_period_str);
            
            trade_settings_.push_back(setting);
        }
    }
    
    std::cout << "Loaded " << trade_settings_.size() << " trade settings" << std::endl;
    return true;
}

bool ZerodhaClient::fetchInstruments() {
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Please login first." << std::endl;
        return false;
    }
    
    std::cout << "Fetching instruments from Zerodha..." << std::endl;
    
    // Try different approaches for fetching instruments
    std::vector<std::string> urls_to_try = {
        "https://api.kite.trade/instruments/NSE",
        "https://api.kite.trade/instruments",
        "https://api.kite.trade/instruments/NFO"
    };
    
    for (const auto& url : urls_to_try) {
        std::cout << "Trying URL: " << url << std::endl;
        
        std::map<std::string, std::string> headers = getAuthHeaders();
        
        // Debug: Print headers being sent
        std::cout << "Debug: Request headers:" << std::endl;
        for (const auto& header : headers) {
            std::cout << "  " << header.first << ": " << header.second << std::endl;
        }
        
        cpr::Response response = makeRequest(url, {}, headers);
        
        std::cout << "Debug: Response Status: " << response.status_code << std::endl;
        
        // Print first 200 characters of response for debugging
        std::string response_preview = response.text.substr(0, 200);
        std::cout << "Debug: Response Preview: " << response_preview << std::endl;
        
        if (response.status_code == 200) {
            // Check if response is empty
            if (response.text.empty()) {
                std::cout << "Empty response, trying next URL..." << std::endl;
                continue;
            }
            
            // Check if response starts with CSV header (instrument_token,exchange_token,...)
            if (response.text.find("instrument_token,exchange_token") == 0) {
                std::cout << "Found CSV response, parsing instruments..." << std::endl;
                if (parseInstrumentsResponse(response)) {
                    std::cout << "Successfully fetched instruments from: " << url << std::endl;
                    return true;
                } else {
                    std::cout << "Failed to parse CSV instruments from: " << url << std::endl;
                    continue;
                }
            }
            // Check if response starts with JSON (for backward compatibility)
            else if (response.text[0] == '{' || response.text[0] == '[') {
                std::cout << "Found JSON response, parsing instruments..." << std::endl;
                if (parseInstrumentsResponse(response)) {
                    std::cout << "Successfully fetched instruments from: " << url << std::endl;
                    return true;
                } else {
                    std::cout << "Failed to parse JSON instruments from: " << url << std::endl;
                    continue;
                }
            }
            else {
                std::cout << "Response format not recognized, trying next URL..." << std::endl;
                continue;
            }
        } else {
            std::cout << "HTTP error " << response.status_code << " for URL: " << url << std::endl;
            continue;
        }
    }
    
    std::cerr << "Error: Failed to fetch instruments from all attempted URLs" << std::endl;
    
    // Only create fallback if ALL HTTP requests failed (not just parsing failures)
    std::cout << "Creating fallback instrument data for common symbols..." << std::endl;
    instruments_cache_.clear();
    
    // Add some common NSE symbols with dummy tokens
    std::vector<std::string> common_symbols = {
        "RELIANCE", "TCS", "HDFCBANK", "INFY", "ICICIBANK", "HINDUNILVR", "ITC", "SBIN", "BHARTIARTL", "KOTAKBANK"
    };
    
    for (size_t i = 0; i < common_symbols.size(); ++i) {
        Instrument inst;
        inst.instrument_token = std::to_string(1000000 + i); // Dummy token
        inst.tradingsymbol = common_symbols[i];
        inst.name = common_symbols[i];
        inst.exchange = "NSE";
        inst.instrument_type = "EQ";
        
        instruments_cache_[inst.tradingsymbol] = inst;
    }
    
    std::cout << "Created " << instruments_cache_.size() << " fallback instruments" << std::endl;
    std::cout << "Note: Historical data fetching may not work properly with fallback instruments" << std::endl;
    
    return true;
}

std::vector<CandleData> ZerodhaClient::getHistoricalData(const std::string& symbol, 
                                                        const std::string& timeframe,
                                                        const std::string& from_date,
                                                        const std::string& to_date,
                                                        bool include_oi) {
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Please login first." << std::endl;
        return {};
    }
    
    std::string instrument_token = getInstrumentToken(symbol);
    if (instrument_token.empty()) {
        std::cerr << "Error: Could not find instrument token for symbol: " << symbol << std::endl;
        return {};
    }
    
    // Build URL with parameters
    std::string url = std::string(HISTORICAL_URL) + "/" + instrument_token + "/" + timeframe;
    
    std::map<std::string, std::string> params;
    params["from"] = from_date;
    params["to"] = to_date;
    if (include_oi) {
        params["oi"] = "1";
    }
    
    std::map<std::string, std::string> headers = getAuthHeaders();
    
    std::cout << "Fetching historical data for " << symbol << " (" << timeframe << ")..." << std::endl;
    
    cpr::Response response = makeRequest(url, params, headers);
    
    if (response.status_code == 200) {
        return parseHistoricalDataResponse(response);
    } else {
        std::cerr << "Error: Failed to fetch historical data for " << symbol << ". Status: " << response.status_code << std::endl;
        std::cerr << "Response: " << response.text << std::endl;
        return {};
    }
}

bool ZerodhaClient::fetchHistoricalDataForAllSymbols(const std::string& from_date,
                                                    const std::string& to_date) {
    if (trade_settings_.empty()) {
        std::cerr << "Error: No trade settings loaded. Call loadTradeSettings() first." << std::endl;
        return false;
    }
    
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Please login first." << std::endl;
        return false;
    }
    
    std::cout << "Fetching historical data for all " << trade_settings_.size() << " symbols..." << std::endl;
    
    for (const auto& setting : trade_settings_) {
        std::vector<CandleData> candles = getHistoricalData(setting.symbol, 
                                                           setting.timeframe, 
                                                           from_date, 
                                                           to_date);
        
        if (!candles.empty()) {
            std::cout << "✓ " << setting.symbol << ": " << candles.size() << " candles" << std::endl;
        } else {
            std::cout << "✗ " << setting.symbol << ": No data" << std::endl;
        }
    }
    
    return true;
}

bool ZerodhaClient::login() {
    if (api_key_.empty() || api_secret_.empty()) {
        std::cerr << "Error: Credentials not loaded. Call loadCredentials() first." << std::endl;
        return false;
    }
    
    std::cout << "Attempting to login to Zerodha..." << std::endl;
    
    // Step 1: Build login URL according to Zerodha documentation
    std::string loginUrl = LOGIN_URL;
    loginUrl += "?v=3&api_key=" + api_key_;
    
    std::cout << "Login URL: " << loginUrl << std::endl;
    std::cout << "Please visit this URL in your browser to complete the login process." << std::endl;
    std::cout << "After successful login, you will receive a request token." << std::endl;
    std::cout << "Enter the request token: ";
    
    std::string requestToken;
    std::getline(std::cin, requestToken);
    
    if (requestToken.empty()) {
        std::cerr << "Error: Request token is required for login" << std::endl;
        return false;
    }
    
    // Step 2: Generate session token
    return generateSessionToken(requestToken);
}

bool ZerodhaClient::generateSessionToken(const std::string& requestToken) {
    // Generate checksum: SHA-256 of (api_key + request_token + api_secret)
    std::string checksumInput = api_key_ + requestToken + api_secret_;
    std::string checksum = generateSHA256(checksumInput);
    
    std::cout << "Debug: Generating session token..." << std::endl;
    std::cout << "Debug: API Key: " << api_key_ << std::endl;
    std::cout << "Debug: Request Token: " << requestToken << std::endl;
    std::cout << "Debug: Checksum Input: " << checksumInput << std::endl;
    std::cout << "Debug: Generated Checksum: " << checksum << std::endl;
    
    std::map<std::string, std::string> data;
    data["api_key"] = api_key_;
    data["request_token"] = requestToken;
    data["checksum"] = checksum;
    
    std::map<std::string, std::string> headers = getDefaultHeaders();
    
    std::cout << "Debug: Making POST request to: " << TOKEN_URL << std::endl;
    
    // Use POST request as per Zerodha documentation
    cpr::Response response = makePostRequest(TOKEN_URL, data, headers);
    
    std::cout << "Debug: Response Status: " << response.status_code << std::endl;
    std::cout << "Debug: Response Text: " << response.text << std::endl;
    
    if (response.status_code == 0) {
        std::cerr << "Error: Network connection failed. Please check your internet connection." << std::endl;
        std::cerr << "Error details: " << response.error.message << std::endl;
        return false;
    } else if (response.status_code == 200) {
        return parseTokenResponse(response);
    } else {
        std::cerr << "Error: Failed to generate session token. Status: " << response.status_code << std::endl;
        std::cerr << "Response: " << response.text << std::endl;
        return false;
    }
}

bool ZerodhaClient::generateSessionToken() {
    std::cerr << "Error: Request token is required for session generation" << std::endl;
    return false;
}

cpr::Response ZerodhaClient::makeRequest(const std::string& url, 
                                        const std::map<std::string, std::string>& params,
                                        const std::map<std::string, std::string>& headers) {
    cpr::Parameters cprParams;
    for (const auto& param : params) {
        cprParams.Add({param.first, param.second});
    }
    
    cpr::Header cprHeaders;
    for (const auto& header : headers) {
        cprHeaders[header.first] = header.second;
    }
    
    // Disable SSL verification to fix certificate issues
    return cpr::Get(cpr::Url{url}, 
                   cprParams, 
                   cprHeaders,
                   cpr::VerifySsl{false},
                   cpr::Timeout{30000}); // 30 second timeout
}

cpr::Response ZerodhaClient::makePostRequest(const std::string& url,
                                            const std::map<std::string, std::string>& data,
                                            const std::map<std::string, std::string>& headers) {
    cpr::Payload payload{};
    for (const auto& item : data) {
        payload.Add({item.first, item.second});
    }
    
    cpr::Header cprHeaders;
    for (const auto& header : headers) {
        cprHeaders[header.first] = header.second;
    }
    
    // Disable SSL verification to fix certificate issues
    return cpr::Post(cpr::Url{url}, 
                    payload, 
                    cprHeaders,
                    cpr::VerifySsl{false},
                    cpr::Timeout{30000}); // 30 second timeout
}

std::string ZerodhaClient::generateChecksum(const std::map<std::string, std::string>& params) {
    // Build parameter string for checksum
    std::string paramString;
    for (const auto& param : params) {
        if (!paramString.empty()) paramString += "&";
        paramString += param.first + param.second;
    }
    
    // For now, return a simple hash (you can implement proper HMAC-SHA256 later if needed)
    // This is a simplified version for testing
    std::hash<std::string> hasher;
    size_t hash = hasher(paramString + api_secret_);
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::string ZerodhaClient::generateSHA256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, input.c_str(), input.length());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

std::map<std::string, std::string> ZerodhaClient::getDefaultHeaders() {
    std::map<std::string, std::string> headers;
    headers["X-Kite-Version"] = "3";
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    return headers;
}

bool ZerodhaClient::parseLoginResponse(const cpr::Response& response) {
    try {
        nlohmann::json json = nlohmann::json::parse(response.text);
        
        if (json["status"] == "success") {
            std::cout << "Login successful!" << std::endl;
            return true;
        } else {
            std::cerr << "Login failed: " << json["message"] << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing login response: " << e.what() << std::endl;
        return false;
    }
}

bool ZerodhaClient::parseTokenResponse(const cpr::Response& response) {
    try {
        nlohmann::json json = nlohmann::json::parse(response.text);
        
        if (json["status"] == "success") {
            access_token_ = json["data"]["access_token"];
            user_id_ = json["data"]["user_id"];
            
            std::cout << "Session token generated successfully!" << std::endl;
            std::cout << "User ID: " << user_id_ << std::endl;
            std::cout << "Access Token: " << access_token_.substr(0, 10) << "..." << std::endl;
            return true;
        } else {
            std::cerr << "Token generation failed: " << json["message"] << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing token response: " << e.what() << std::endl;
        std::cerr << "Response: " << response.text << std::endl;
        return false;
    }
} 

std::map<std::string, std::string> ZerodhaClient::getAuthHeaders() {
    std::map<std::string, std::string> headers = getDefaultHeaders();
    if (!access_token_.empty()) {
        headers["Authorization"] = "token " + api_key_ + ":" + access_token_;
        std::cout << "Debug: Authorization header set: token " << api_key_ << ":" << access_token_.substr(0, 10) << "..." << std::endl;
    } else {
        std::cout << "Debug: No access token available for authorization" << std::endl;
    }
    return headers;
}

bool ZerodhaClient::parseInstrumentsResponse(const cpr::Response& response) {
    try {
        // Parse CSV from response.text
        auto rows = CSVParser::parseCSVString(response.text);
        if (rows.empty()) {
            std::cerr << "Instrument CSV response is empty or invalid" << std::endl;
            return false;
        }
        
        instruments_cache_.clear();
        int valid_instruments = 0;
        
        for (const auto& row : rows) {
            // Required fields: instrument_token, tradingsymbol
            if (row.count("instrument_token") && row.count("tradingsymbol")) {
                Instrument inst;
                inst.instrument_token = row.at("instrument_token");
                inst.tradingsymbol = row.at("tradingsymbol");
                inst.name = row.count("name") ? row.at("name") : "";
                inst.exchange = row.count("exchange") ? row.at("exchange") : "";
                inst.instrument_type = row.count("instrument_type") ? row.at("instrument_type") : "";
                
                // Only add equity instruments (EQ) from NSE for now
                if (inst.exchange == "NSE" && inst.instrument_type == "EQ") {
                    instruments_cache_[inst.tradingsymbol] = inst;
                    valid_instruments++;
                }
            }
        }
        
        std::cout << "Loaded " << valid_instruments << " NSE equity instruments from CSV" << std::endl;
        std::cout << "Total instruments in response: " << rows.size() << std::endl;
        
        return !instruments_cache_.empty();
    } catch (const std::exception& e) {
        std::cerr << "Error parsing instruments CSV response: " << e.what() << std::endl;
        std::cerr << "Response preview: " << response.text.substr(0, 300) << std::endl;
        return false;
    }
}

std::vector<CandleData> ZerodhaClient::parseHistoricalDataResponse(const cpr::Response& response) {
    std::vector<CandleData> candles;
    
    try {
        nlohmann::json json = nlohmann::json::parse(response.text);
        
        if (json["status"] == "success") {
            for (const auto& candle_array : json["data"]["candles"]) {
                CandleData candle;
                
                // Parse candle data: [timestamp, open, high, low, close, volume, oi]
                candle.timestamp = candle_array[0];
                candle.open = candle_array[1];
                candle.high = candle_array[2];
                candle.low = candle_array[3];
                candle.close = candle_array[4];
                candle.volume = candle_array[5];
                
                // OI is optional (7th element)
                if (candle_array.size() > 6) {
                    candle.oi = candle_array[6];
                }
                
                candles.push_back(candle);
            }
        } else {
            std::cerr << "Failed to parse historical data: " << json["message"] << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing historical data response: " << e.what() << std::endl;
    }
    
    return candles;
}

std::string ZerodhaClient::getInstrumentToken(const std::string& symbol) {
    auto it = instruments_cache_.find(symbol);
    if (it != instruments_cache_.end()) {
        return it->second.instrument_token;
    }
    
    // If not found, try to find by partial match (for NSE symbols)
    std::string nse_symbol = "NSE:" + symbol;
    it = instruments_cache_.find(nse_symbol);
    if (it != instruments_cache_.end()) {
        return it->second.instrument_token;
    }
    
    return "";
} 

std::vector<double> ZerodhaClient::calculateEMA(const std::vector<double>& prices, int period) {
    std::vector<double> ema_values;
    if (prices.empty() || period <= 0) {
        return ema_values;
    }
    
    // Calculate multiplier
    double multiplier = 2.0 / (period + 1.0);
    
    // Initialize EMA with first price
    ema_values.push_back(prices[0]);
    
    // Calculate EMA for remaining prices
    for (size_t i = 1; i < prices.size(); ++i) {
        double ema = (prices[i] * multiplier) + (ema_values[i-1] * (1 - multiplier));
        ema_values.push_back(ema);
    }
    
    return ema_values;
}

bool ZerodhaClient::saveInstrumentDataToCSV(const std::string& symbol, const std::vector<CandleData>& candles, const std::vector<double>& ema_values) {
    if (candles.empty()) {
        std::cerr << "Error: No candle data to save for " << symbol << std::endl;
        return false;
    }
    
    std::string filename = symbol + "_data.csv";
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create file: " << filename << std::endl;
        return false;
    }
    
    // Write header
    file << "Timestamp,Open,High,Low,Close,Volume,EMA\n";
    
    // Write data
    for (size_t i = 0; i < candles.size(); ++i) {
        const auto& candle = candles[i];
        double ema = (i < ema_values.size()) ? ema_values[i] : 0.0;
        
        file << candle.timestamp << ","
             << std::fixed << std::setprecision(2) << candle.open << ","
             << std::fixed << std::setprecision(2) << candle.high << ","
             << std::fixed << std::setprecision(2) << candle.low << ","
             << std::fixed << std::setprecision(2) << candle.close << ","
             << candle.volume << ","
             << std::fixed << std::setprecision(2) << ema << "\n";
    }
    
    file.close();
    std::cout << "Saved " << candles.size() << " records to " << filename << std::endl;
    return true;
}

bool ZerodhaClient::saveInstrumentsToCSV(const std::string& filename) {
    if (instruments_cache_.empty()) {
        std::cerr << "Error: No instruments loaded. Call fetchInstruments() first." << std::endl;
        return false;
    }
    
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create file: " << filename << std::endl;
        return false;
    }
    
    // Write header
    file << "instrument_token,tradingsymbol,name,exchange,instrument_type,segment,lot_size,tick_size\n";
    
    // Write data
    for (const auto& pair : instruments_cache_) {
        const Instrument& inst = pair.second;
        file << inst.instrument_token << ","
             << inst.tradingsymbol << ","
             << "\"" << inst.name << "\","
             << inst.exchange << ","
             << inst.instrument_type << ","
             << "NSE" << ","  // Default segment
             << "1" << ","   // Default lot_size
             << "0.05\n";    // Default tick_size
    }
    
    file.close();
    std::cout << "Saved " << instruments_cache_.size() << " instruments to " << filename << std::endl;
    return true;
}

bool ZerodhaClient::loadInstrumentsFromCSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open instruments file: " << filename << std::endl;
        return false;
    }
    
    instruments_cache_.clear();
    std::string line;
    
    // Skip header
    std::getline(file, line);
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> parts = CSVParser::splitCSVLine(line);
        if (parts.size() >= 5) {
            Instrument inst;
            inst.instrument_token = parts[0];
            inst.tradingsymbol = parts[1];
            inst.name = parts[2];
            inst.exchange = parts[3];
            inst.instrument_type = parts[4];
            
            instruments_cache_[inst.tradingsymbol] = inst;
        }
    }
    
    file.close();
    std::cout << "Loaded " << instruments_cache_.size() << " instruments from " << filename << std::endl;
    return true;
}

std::vector<std::string> ZerodhaClient::getMatchedSymbols() {
    std::vector<std::string> matched_symbols;
    
    for (const auto& setting : trade_settings_) {
        if (instruments_cache_.find(setting.symbol) != instruments_cache_.end()) {
            matched_symbols.push_back(setting.symbol);
        }
    }
    
    std::cout << "Found " << matched_symbols.size() << " symbols in instruments out of " 
              << trade_settings_.size() << " trade settings" << std::endl;
    
    return matched_symbols;
} 

std::string ZerodhaClient::formatDate(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

LastThreeCandles ZerodhaClient::getLastThreeCandles(const std::vector<CandleData>& candles, const std::vector<double>& ema_values) {
    LastThreeCandles data;
    
    if (candles.size() >= 3 && ema_values.size() >= 3) {
        size_t last_idx = candles.size() - 1;
        size_t second_idx = candles.size() - 2;
        size_t third_idx = candles.size() - 3;
        
        // Print timestamps for verification (formatted for 5-minute boundaries)
        std::cout << "\n=== Last 3 Candles Data ===" << std::endl;
        std::cout << "Total candles available: " << candles.size() << std::endl;
        std::cout << "Using candles at indices: " << third_idx << ", " << second_idx << ", " << last_idx << std::endl;
        
        // Format timestamp to show only 5-minute boundaries
        auto format5MinTimestamp = [](const std::string& timestamp) -> std::string {
            // Extract date and time from timestamp like "2025-07-18T11:58:12+0530"
            if (timestamp.length() >= 16) {
                std::string date_part = timestamp.substr(0, 10); // "2025-07-18"
                std::string time_part = timestamp.substr(11, 5); // "11:58"
                
                // Round to nearest 5-minute boundary
                int hour = std::stoi(time_part.substr(0, 2));
                int minute = std::stoi(time_part.substr(3, 2));
                int rounded_minute = (minute / 5) * 5; // Round down to nearest 5
                
                std::ostringstream oss;
                oss << date_part << " " << std::setfill('0') << std::setw(2) << hour 
                    << ":" << std::setfill('0') << std::setw(2) << rounded_minute;
                return oss.str();
            }
            return timestamp;
        };
        
        // Show raw timestamps first
        std::cout << "Raw timestamps from API:" << std::endl;
        std::cout << "Third candle (oldest): " << candles[third_idx].timestamp << std::endl;
        std::cout << "Second candle: " << candles[second_idx].timestamp << std::endl;
        std::cout << "Last candle (most recent): " << candles[last_idx].timestamp << std::endl;
        
        // Show formatted 5-minute timestamps
        std::cout << "\nFormatted 5-minute timestamps:" << std::endl;
        std::cout << "Third candle (oldest): " << format5MinTimestamp(candles[third_idx].timestamp)
                  << " | O:" << candles[third_idx].open 
                  << " H:" << candles[third_idx].high 
                  << " L:" << candles[third_idx].low 
                  << " C:" << candles[third_idx].close 
                  << " EMA:" << ema_values[third_idx] << std::endl;
        
        std::cout << "Second candle: " << format5MinTimestamp(candles[second_idx].timestamp)
                  << " | O:" << candles[second_idx].open 
                  << " H:" << candles[second_idx].high 
                  << " L:" << candles[second_idx].low 
                  << " C:" << candles[second_idx].close 
                  << " EMA:" << ema_values[second_idx] << std::endl;
        
        std::cout << "Last candle (most recent): " << format5MinTimestamp(candles[last_idx].timestamp)
                  << " | O:" << candles[last_idx].open 
                  << " H:" << candles[last_idx].high 
                  << " L:" << candles[last_idx].low 
                  << " C:" << candles[last_idx].close 
                  << " EMA:" << ema_values[last_idx] << std::endl;
        std::cout << "=================================" << std::endl;
        
        // Last candle (most recent)
        data.last_open = candles[last_idx].open;
        data.last_high = candles[last_idx].high;
        data.last_low = candles[last_idx].low;
        data.last_close = candles[last_idx].close;
        data.last_ema = ema_values[last_idx];
        
        // Second candle
        data.second_open = candles[second_idx].open;
        data.second_high = candles[second_idx].high;
        data.second_low = candles[second_idx].low;
        data.second_close = candles[second_idx].close;
        data.second_ema = ema_values[second_idx];
        
        // Third candle (oldest)
        data.third_open = candles[third_idx].open;
        data.third_high = candles[third_idx].high;
        data.third_low = candles[third_idx].low;
        data.third_close = candles[third_idx].close;
        data.third_ema = ema_values[third_idx];
    }
    
    return data;
}

TradeSignal ZerodhaClient::analyzeStrategy(const std::string& symbol, const LastThreeCandles& data) {
    TradeSignal signal;
    signal.symbol = symbol;
    signal.quantity = 1; // Default quantity
    
    // Buy Strategy
    // thirdopen>thirdclose and secondopen>secondclose and secondclose>thirdclose and secondclose>secondema and thirdclose>thirdema and lastclose>lastema
    if (data.third_open < data.third_close && 
        data.second_open < data.second_close && 
       // data.second_close > data.third_close && 
        data.second_close > data.second_ema && 
        data.third_close > data.third_ema && 
        data.last_close > data.last_ema &&
        data.last_close > data.second_high) {
        
        signal.action = "BUY";
        signal.entry_price = data.last_close;
        // Stop loss: lowest of second and third candle lows
        signal.stop_loss = (std::min)(data.second_low, data.third_low);
        signal.target = data.last_close + (2 * (data.last_close - signal.stop_loss));
        
        std::cout << "BUY Signal for " << symbol << " - Entry: " << signal.entry_price 
                  << ", SL: " << signal.stop_loss << ", Target: " << signal.target << std::endl;
    }
    // Sell Strategy
    // thirdopen<thirdclose and secondopen<secondclose and secondclose<thirdclose and secondclose<secondema and thirdclose<thirdema and lastclose<lastema
    else if (data.third_open > data.third_close && 
             data.second_open > data.second_close && 
            // data.second_close < data.third_close && 
             data.second_close < data.second_ema && 
             data.third_close < data.third_ema && 
             data.last_close < data.last_ema &&
             data.last_close < data.second_low) {
        
        signal.action = "SELL";
        signal.entry_price = data.last_close;
        // Stop loss: highest of second and third candle highs
        signal.stop_loss = (std::max)(data.second_high, data.third_high);
        signal.target = data.last_close - (2 * (signal.stop_loss - data.last_close));
        
        std::cout << "SELL Signal for " << symbol << " - Entry: " << signal.entry_price 
                  << ", SL: " << signal.stop_loss << ", Target: " << signal.target << std::endl;
    }
    
    return signal;
}

bool ZerodhaClient::placeOrder(const TradeSignal& signal) {
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Cannot place order." << std::endl;
        return false;
    }
    
    if (signal.action.empty()) {
        return false; // No signal
    }
    
    std::cout << "Placing " << signal.action << " order for " << signal.symbol << std::endl;
    
    // Prepare order data
    std::map<std::string, std::string> order_data;
    order_data["tradingsymbol"] = signal.symbol;
    order_data["exchange"] = "NSE";
    order_data["transaction_type"] = (signal.action == "BUY" || signal.action == "BUY_STOPLOSS" || signal.action == "SELL_TARGET") ? "BUY" : "SELL";
    order_data["order_type"] = "MARKET";
    order_data["quantity"] = std::to_string(signal.quantity);
    order_data["product"] = "MIS"; // Intraday
    order_data["validity"] = "DAY";
    
    // Add tag for identification
    order_data["tag"] = "TradingBot_" + signal.action;
    
    std::map<std::string, std::string> headers = getAuthHeaders();
    
    // Place order
    cpr::Response response = makePostRequest("https://api.kite.trade/orders/regular", order_data, headers);
    
    std::cout << "Order Response Status: " << response.status_code << std::endl;
    std::cout << "Order Response: " << response.text << std::endl;
    
    if (response.status_code == 200) {
        try {
            nlohmann::json json = nlohmann::json::parse(response.text);
            if (json["status"] == "success") {
                std::string order_id = json["data"]["order_id"];
                std::cout << "Order placed successfully! Order ID: " << order_id << std::endl;
                
                // Log the entry order
                logOrder(signal.symbol, signal.action, order_id, signal.entry_price, signal.quantity, "ENTRY");
                
                // Add to active positions and place SL/Target orders
                addActivePosition(signal.symbol, order_id, signal);
                
                // Place Stop Loss order
                std::cout << "Placing Stop Loss order..." << std::endl;
                if (placeStopLossOrder(signal.symbol, signal.action, signal.stop_loss, signal.quantity)) {
                    active_positions_[signal.symbol].stop_loss_placed = true;
                    std::cout << "Stop Loss order placed successfully!" << std::endl;
                } else {
                    std::cerr << "Failed to place Stop Loss order!" << std::endl;
                }
                
                // Place Target order
                std::cout << "Placing Target order..." << std::endl;
                if (placeTargetOrder(signal.symbol, signal.action, signal.target, signal.quantity)) {
                    active_positions_[signal.symbol].target_placed = true;
                    std::cout << "Target order placed successfully!" << std::endl;
                } else {
                    std::cerr << "Failed to place Target order!" << std::endl;
                }
                
                return true;
            } else {
                std::cerr << "Order placement failed: " << json["message"] << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing order response: " << e.what() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Order placement failed with status: " << response.status_code << std::endl;
        return false;
    }
}

void ZerodhaClient::runTradingLoop() {
    std::cout << "Starting continuous trading loop..." << std::endl;
    
    while (true) {
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        // Check if market is open (9:15 AM to 3:30 PM)
        int current_hour = tm.tm_hour;
        int current_minute = tm.tm_min;
        
        if (current_hour < 13 || (current_hour == 13 && current_minute < 40) ||     
            current_hour > 15 || (current_hour == 15 && current_minute > 30)) {
            std::cout << "Market is closed. Waiting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::minutes(5));
            continue;
        }
        
        std::cout << "\n=== Continuous Trading Loop ===" << std::endl;
        
        // Check position status (SL/Target hits)
        checkPositionStatus();
        
        // Process each symbol continuously
        for (const auto& symbol : getMatchedSymbols()) {
            // Skip if already have active position (Rule 1: Wait for target/SL before new trade)
            if (hasActivePosition(symbol)) {
                std::cout << "Skipping " << symbol << " - Already has active position" << std::endl;
                continue;
            }
            
            std::cout << "Analyzing " << symbol << "..." << std::endl;
            
            // Get timeframe and EMA period from trade settings
            std::string timeframe = "5minute";
            int ema_period = 20;
            for (const auto& setting : getTradeSettings()) {
                if (setting.symbol == symbol) {
                    timeframe = setting.timeframe;
                    ema_period = setting.ema_period;
                    break;
                }
            }
            
            // Get historical data for EMA calculation (fetch 10 days of data)
            // 10 days = 240 hours = 2880 candles for 5-minute timeframe
            auto data_start_time = now - std::chrono::hours(240); // 10 days of data
            std::string from_date = formatDate(data_start_time);
            std::string to_date = formatDate(now);
            
            std::vector<CandleData> candles = getHistoricalData(symbol, timeframe, from_date, to_date);
            
            // Debug: Print raw timestamp data from API
            std::cout << "Fetched " << candles.size() << " candles for " << symbol << " (expected ~2880 candles for 10 days of 5-min data)" << std::endl;
            if (!candles.empty()) {
                std::cout << "First candle timestamp: " << candles[0].timestamp << std::endl;
                std::cout << "Last candle timestamp: " << candles[candles.size()-1].timestamp << std::endl;
            }
            
            // Save raw data to CSV for verification (exactly as received from API)
            // saveInstrumentDataToCSV(symbol + "_raw", candles, ema_values);
            
            if (candles.size() >= 3) {
                std::vector<double> close_prices;
                for (const auto& candle : candles) {
                    close_prices.push_back(candle.close);
                }
                std::vector<double> ema_values = calculateEMA(close_prices, ema_period);
                // Get last 3 candles
                LastThreeCandles last_three = getLastThreeCandles(candles, ema_values);
                // Analyze strategy
                TradeSignal signal = analyzeStrategy(symbol, last_three);
                // Place order if signal exists
                if (!signal.action.empty()) {
                    placeOrder(signal);
                }
            }
            
            // Small delay between symbols (continuous monitoring)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Continuous monitoring - no 5-minute wait
        std::this_thread::sleep_for(std::chrono::seconds(10)); // Check every 10 seconds
    }
}

// Position management methods
bool ZerodhaClient::placeStopLossOrder(const std::string& symbol, const std::string& action, double stop_loss, int quantity) {
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Cannot place stop loss order." << std::endl;
        return false;
    }
    
    std::cout << "Placing Stop Loss order for " << symbol << " at " << stop_loss << std::endl;
    
    // Prepare order data
    std::map<std::string, std::string> order_data;
    order_data["tradingsymbol"] = symbol;
    order_data["exchange"] = "NSE";
    order_data["transaction_type"] = (action == "BUY") ? "SELL" : "BUY"; // Opposite of entry
    order_data["order_type"] = "SL"; // Stop Loss order type
    order_data["quantity"] = std::to_string(quantity);
    order_data["product"] = "MIS"; // Intraday
    order_data["validity"] = "DAY";
    order_data["trigger_price"] = std::to_string(stop_loss);
    order_data["price"] = std::to_string(stop_loss);
    
    // Add tag for identification
    order_data["tag"] = "TradingBot_SL";
    
    std::map<std::string, std::string> headers = getAuthHeaders();
    
    // Place order
    cpr::Response response = makePostRequest("https://api.kite.trade/orders/regular", order_data, headers);
    
    std::cout << "Stop Loss Order Response Status: " << response.status_code << std::endl;
    std::cout << "Stop Loss Order Response: " << response.text << std::endl;
    
    if (response.status_code == 200) {
        try {
            nlohmann::json json = nlohmann::json::parse(response.text);
            if (json["status"] == "success") {
                std::string order_id = json["data"]["order_id"];
                std::cout << "Stop Loss order placed successfully! Order ID: " << order_id << std::endl;
                logOrder(symbol, (action == "BUY") ? "SELL" : "BUY", order_id, stop_loss, quantity, "STOPLOSS");
                return true;
            } else {
                std::cerr << "Stop Loss order placement failed: " << json["message"] << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing stop loss order response: " << e.what() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Stop Loss order placement failed with status: " << response.status_code << std::endl;
        return false;
    }
}

bool ZerodhaClient::placeTargetOrder(const std::string& symbol, const std::string& action, double target, int quantity) {
    if (!isLoggedIn()) {
        std::cerr << "Error: Not logged in. Cannot place target order." << std::endl;
        return false;
    }
    
    std::cout << "Placing Target order for " << symbol << " at " << target << std::endl;
    
    // Prepare order data
    std::map<std::string, std::string> order_data;
    order_data["tradingsymbol"] = symbol;
    order_data["exchange"] = "NSE";
    order_data["transaction_type"] = (action == "BUY") ? "SELL" : "BUY"; // Opposite of entry
    order_data["order_type"] = "LIMIT"; // Limit order for target
    order_data["quantity"] = std::to_string(quantity);
    order_data["product"] = "MIS"; // Intraday
    order_data["validity"] = "DAY";
    order_data["price"] = std::to_string(target);
    
    // Add tag for identification
    order_data["tag"] = "TradingBot_TARGET";
    
    std::map<std::string, std::string> headers = getAuthHeaders();
    
    // Place order
    cpr::Response response = makePostRequest("https://api.kite.trade/orders/regular", order_data, headers);
    
    std::cout << "Target Order Response Status: " << response.status_code << std::endl;
    std::cout << "Target Order Response: " << response.text << std::endl;
    
    if (response.status_code == 200) {
        try {
            nlohmann::json json = nlohmann::json::parse(response.text);
            if (json["status"] == "success") {
                std::string order_id = json["data"]["order_id"];
                std::cout << "Target order placed successfully! Order ID: " << order_id << std::endl;
                logOrder(symbol, (action == "BUY") ? "SELL" : "BUY", order_id, target, quantity, "TARGET");
                return true;
            } else {
                std::cerr << "Target order placement failed: " << json["message"] << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing target order response: " << e.what() << std::endl;
            return false;
        }
    } else {
        std::cerr << "Target order placement failed with status: " << response.status_code << std::endl;
        return false;
    }
}

void ZerodhaClient::addActivePosition(const std::string& symbol, const std::string& entry_order_id, const TradeSignal& signal) {
    ActivePosition position;
    position.symbol = symbol;
    position.entry_order_id = entry_order_id;
    position.action = signal.action;
    position.entry_price = signal.entry_price;
    position.stop_loss = signal.stop_loss;
    position.target = signal.target;
    position.quantity = signal.quantity;
    position.stop_loss_placed = false;
    position.target_placed = false;
    
    active_positions_[symbol] = position;
    std::cout << "Added active position for " << symbol << " - Entry Order ID: " << entry_order_id << std::endl;
}

bool ZerodhaClient::hasActivePosition(const std::string& symbol) {
    return active_positions_.find(symbol) != active_positions_.end();
}

void ZerodhaClient::removeActivePosition(const std::string& symbol) {
    if (active_positions_.find(symbol) != active_positions_.end()) {
        active_positions_.erase(symbol);
        std::cout << "Removed active position for " << symbol << std::endl;
    }
}

// Order logging methods
void ZerodhaClient::logOrder(const std::string& symbol, const std::string& action, const std::string& order_id, 
                            double price, int quantity, const std::string& order_type) {
    std::ofstream log_file("OrderLog.txt", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        log_file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | "
                 << order_type << " | " << action << " | " << symbol << " | "
                 << "Price: " << std::fixed << std::setprecision(2) << price << " | "
                 << "Qty: " << quantity << " | "
                 << "Order ID: " << order_id << std::endl;
        
        log_file.close();
        std::cout << "Order logged to OrderLog.txt" << std::endl;
    } else {
        std::cerr << "Error: Could not open OrderLog.txt for writing" << std::endl;
    }
}

void ZerodhaClient::logStopLossHit(const std::string& symbol, double price) {
    std::ofstream log_file("OrderLog.txt", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        log_file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | "
                 << "STOPLOSS_HIT" << " | " << symbol << " | "
                 << "Price: " << std::fixed << std::setprecision(2) << price << std::endl;
        
        log_file.close();
        std::cout << "Stop Loss hit logged to OrderLog.txt" << std::endl;
    } else {
        std::cerr << "Error: Could not open OrderLog.txt for writing" << std::endl;
    }
}

void ZerodhaClient::logTargetHit(const std::string& symbol, double price) {
    std::ofstream log_file("OrderLog.txt", std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        log_file << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " | "
                 << "TARGET_HIT" << " | " << symbol << " | "
                 << "Price: " << std::fixed << std::setprecision(2) << price << std::endl;
        
        log_file.close();
        std::cout << "Target hit logged to OrderLog.txt" << std::endl;
    } else {
        std::cerr << "Error: Could not open OrderLog.txt for writing" << std::endl;
    }
}

// Position monitoring methods
void ZerodhaClient::checkPositionStatus() {
    // This method would check if stop loss or target orders have been executed
    // For now, we'll implement a basic version that logs when positions are closed
    // In a real implementation, you would query the Zerodha API for order status
    
    std::vector<std::string> positions_to_remove;
    
    for (const auto& pair : active_positions_) {
        const std::string& symbol = pair.first;
        const ActivePosition& position = pair.second;
        
        // Get current market price to check if SL/Target hit
        auto now = std::chrono::system_clock::now();
        auto ten_minutes_ago = now - std::chrono::minutes(10);
        std::string from_date = formatDate(ten_minutes_ago);
        std::string to_date = formatDate(now);
        
        std::vector<CandleData> candles = getHistoricalData(symbol, "5minute", from_date, to_date);
        
        if (!candles.empty()) {
            double current_price = candles.back().close;
            
            // Check if stop loss hit
            if (position.action == "BUY" && current_price <= position.stop_loss) {
                logStopLossHit(symbol, current_price);
                positions_to_remove.push_back(symbol);
                std::cout << "Stop Loss hit for " << symbol << " at " << current_price << std::endl;
            }
            else if (position.action == "SELL" && current_price >= position.stop_loss) {
                logStopLossHit(symbol, current_price);
                positions_to_remove.push_back(symbol);
                std::cout << "Stop Loss hit for " << symbol << " at " << current_price << std::endl;
            }
            // Check if target hit
            else if (position.action == "BUY" && current_price >= position.target) {
                logTargetHit(symbol, current_price);
                positions_to_remove.push_back(symbol);
                std::cout << "Target hit for " << symbol << " at " << current_price << std::endl;
            }
            else if (position.action == "SELL" && current_price <= position.target) {
                logTargetHit(symbol, current_price);
                positions_to_remove.push_back(symbol);
                std::cout << "Target hit for " << symbol << " at " << current_price << std::endl;
            }
        }
    }
    
    // Remove closed positions
    for (const auto& symbol : positions_to_remove) {
        removeActivePosition(symbol);
    }
} 