#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>

// Structure for candle data
struct CandleData {
    std::string timestamp;
    double open;
    double high;
    double low;
    double close;
    int volume;
    int oi; // Open Interest (optional)
    
    CandleData() : open(0), high(0), low(0), close(0), volume(0), oi(0) {}
};

// Structure for trade settings
struct TradeSetting {
    std::string symbol;
    int quantity;
    std::string timeframe;
    int ema_period;
    
    TradeSetting() : quantity(0), ema_period(0) {}
};

// Structure for instrument information
struct Instrument {
    std::string instrument_token;
    std::string tradingsymbol;
    std::string name;
    std::string exchange;
    std::string instrument_type;
    
    Instrument() {}
};

// Structure for last 3 candle data
struct LastThreeCandles {
    // Last candle (most recent)
    double last_open, last_high, last_low, last_close;
    double last_ema;
    
    // Second candle
    double second_open, second_high, second_low, second_close;
    double second_ema;
    
    // Third candle (oldest)
    double third_open, third_high, third_low, third_close;
    double third_ema;
    
    LastThreeCandles() : last_open(0), last_high(0), last_low(0), last_close(0), last_ema(0),
                        second_open(0), second_high(0), second_low(0), second_close(0), second_ema(0),
                        third_open(0), third_high(0), third_low(0), third_close(0), third_ema(0) {}
};

// Structure for trade signals
struct TradeSignal {
    std::string symbol;
    std::string action; // "BUY", "SELL", "BUY_STOPLOSS", "BUY_TARGET", "SELL_STOPLOSS", "SELL_TARGET"
    double entry_price;
    double stop_loss;
    double target;
    int quantity;
    
    TradeSignal() : entry_price(0), stop_loss(0), target(0), quantity(0) {}
};

// Structure for active positions
struct ActivePosition {
    std::string symbol;
    std::string entry_order_id;
    std::string stop_loss_order_id;
    std::string target_order_id;
    std::string action; // "BUY" or "SELL"
    double entry_price;
    double stop_loss;
    double target;
    int quantity;
    bool stop_loss_placed;
    bool target_placed;
    
    ActivePosition() : entry_price(0), stop_loss(0), target(0), quantity(0), 
                      stop_loss_placed(false), target_placed(false) {}
};

class ZerodhaClient {
public:
    ZerodhaClient();
    ~ZerodhaClient() = default;

    // Authentication methods
    bool loadCredentials(const std::string& filename);
    bool login();
    bool generateSessionToken();
    bool generateSessionToken(const std::string& requestToken);
    
    // API methods
    std::string getAccessToken() const { return access_token_; }
    std::string getUserId() const { return user_id_; }
    bool isLoggedIn() const { return !access_token_.empty(); }
    
    // Getter for trade settings
    const std::vector<TradeSetting>& getTradeSettings() const { return trade_settings_; }
    
    // Historical data methods
    bool loadTradeSettings(const std::string& filename);
    bool fetchInstruments();
    std::vector<CandleData> getHistoricalData(const std::string& symbol, 
                                             const std::string& timeframe,
                                             const std::string& from_date,
                                             const std::string& to_date,
                                             bool include_oi = false);
    bool fetchHistoricalDataForAllSymbols(const std::string& from_date,
                                         const std::string& to_date);
    
    // Market quote methods
    double getLTP(const std::string& symbol);
    
    // Instrument management methods
    bool saveInstrumentsToCSV(const std::string& filename);
    bool loadInstrumentsFromCSV(const std::string& filename);
    std::vector<std::string> getMatchedSymbols();
    
    // Data processing methods
    std::vector<double> calculateEMA(const std::vector<double>& prices, int period);
    bool saveInstrumentDataToCSV(const std::string& symbol, const std::vector<CandleData>& candles, const std::vector<double>& ema_values);
    
    // Trading strategy methods
    LastThreeCandles getLastThreeCandles(const std::vector<CandleData>& candles, const std::vector<double>& ema_values);
    TradeSignal analyzeStrategy(const std::string& symbol, const LastThreeCandles& data, double ltp);
    bool placeOrder(const TradeSignal& signal);
    
    // Position management methods
    bool placeStopLossOrder(const std::string& symbol, const std::string& action, double stop_loss, int quantity);
    bool placeTargetOrder(const std::string& symbol, const std::string& action, double target, int quantity);
    void addActivePosition(const std::string& symbol, const std::string& entry_order_id, const TradeSignal& signal);
    bool hasActivePosition(const std::string& symbol);
    void removeActivePosition(const std::string& symbol);
    
    // Order logging methods
    void logOrder(const std::string& symbol, const std::string& action, const std::string& order_id, 
                  double price, int quantity, const std::string& order_type = "ENTRY");
    void logStopLossHit(const std::string& symbol, double price);
    void logTargetHit(const std::string& symbol, double price);
    
    // Position monitoring methods
    void checkPositionStatus();
    void checkPositionStatusWithLTP(const std::string& symbol, double ltp);
    
    // Trading loop method
    void runTradingLoop();
    
    // Helper methods
    std::string formatDate(const std::chrono::system_clock::time_point& time);
    
    // HTTP request methods
    cpr::Response makeRequest(const std::string& url, 
                             const std::map<std::string, std::string>& params = {},
                             const std::map<std::string, std::string>& headers = {});
    
    cpr::Response makePostRequest(const std::string& url,
                                 const std::map<std::string, std::string>& data = {},
                                 const std::map<std::string, std::string>& headers = {});

private:
    // Credentials
    std::string api_key_;
    std::string api_secret_;
    std::string access_token_;
    std::string user_id_;
    
    // Trade settings and instruments
    std::vector<TradeSetting> trade_settings_;
    std::map<std::string, Instrument> instruments_cache_;
    
    // Active positions tracking
    std::map<std::string, ActivePosition> active_positions_;
    
    // API endpoints
    static constexpr const char* LOGIN_URL = "https://kite.zerodha.com/connect/login";
    static constexpr const char* TOKEN_URL = "https://api.kite.trade/session/token";
    static constexpr const char* BASE_URL = "https://api.kite.trade";
    static constexpr const char* INSTRUMENTS_URL = "https://api.kite.trade/instruments/NSE";
    static constexpr const char* HISTORICAL_URL = "https://api.kite.trade/instruments/historical";
    
    // Helper methods
    std::string generateChecksum(const std::map<std::string, std::string>& params);
    std::string generateSHA256(const std::string& input);
    std::map<std::string, std::string> getDefaultHeaders();
    std::map<std::string, std::string> getAuthHeaders();
    bool parseLoginResponse(const cpr::Response& response);
    bool parseTokenResponse(const cpr::Response& response);
    bool parseInstrumentsResponse(const cpr::Response& response);
    std::vector<CandleData> parseHistoricalDataResponse(const cpr::Response& response);
    std::string getInstrumentToken(const std::string& symbol);
}; 