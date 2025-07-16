#include "csv_parser.h"
#include <algorithm>
#include <iostream>

std::map<std::string, std::string> CSVParser::parseCredentials(const std::string& filename) {
    std::map<std::string, std::string> credentials;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return credentials;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> parts = splitCSVLine(line);
        if (parts.size() >= 2) {
            std::string key = trim(parts[0]);
            std::string value = trim(parts[1]);
            credentials[key] = value;
        }
    }
    
    file.close();
    return credentials;
}

std::vector<std::map<std::string, std::string>> CSVParser::parseCSV(const std::string& filename) {
    std::vector<std::map<std::string, std::string>> data;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return data;
    }
    
    std::string line;
    std::vector<std::string> headers;
    bool firstLine = true;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::vector<std::string> parts = splitCSVLine(line);
        
        if (firstLine) {
            // First line contains headers
            for (const auto& header : parts) {
                headers.push_back(trim(header));
            }
            firstLine = false;
        } else {
            // Data lines
            std::map<std::string, std::string> row;
            for (size_t i = 0; i < std::min(headers.size(), parts.size()); ++i) {
                row[headers[i]] = trim(parts[i]);
            }
            data.push_back(row);
        }
    }
    
    file.close();
    return data;
}

std::vector<std::string> CSVParser::splitCSVLine(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string part;
    
    while (std::getline(ss, part, ',')) {
        parts.push_back(part);
    }
    
    return parts;
}

std::string CSVParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
} 

std::vector<std::map<std::string, std::string>> CSVParser::parseCSVString(const std::string& csvText) {
    std::vector<std::map<std::string, std::string>> data;
    std::vector<std::string> headers;
    bool firstLine = true;
    std::stringstream ss(csvText);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts = splitCSVLine(line);
        if (firstLine) {
            for (const auto& header : parts) {
                headers.push_back(trim(header));
            }
            firstLine = false;
        } else {
            std::map<std::string, std::string> row;
            for (size_t i = 0; i < std::min(headers.size(), parts.size()); ++i) {
                row[headers[i]] = trim(parts[i]);
            }
            data.push_back(row);
        }
    }
    return data;
} 