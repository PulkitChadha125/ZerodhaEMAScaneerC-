#pragma once

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>

class CSVParser {
public:
    static std::map<std::string, std::string> parseCredentials(const std::string& filename);
    static std::vector<std::map<std::string, std::string>> parseCSV(const std::string& filename);
    static std::vector<std::map<std::string, std::string>> parseCSVString(const std::string& csvText);
    static std::vector<std::string> splitCSVLine(const std::string& line);
    
private:
    static std::string trim(const std::string& str);
}; 