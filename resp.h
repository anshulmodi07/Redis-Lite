#pragma once

#include <string>
#include <vector>

class RespParser
{
public:
    void feed(const char* data, size_t len);
    bool tryParse(std::vector<std::string>& out);
    size_t bufferedSize() const;

private:
    std::string buffer_;
};

std::string encodeSimpleString(const std::string& value);
std::string encodeOK();
std::string encodeError(const std::string& msg);
std::string encodeInteger(long long value);
std::string encodeBulkString(const std::string& value);
std::string encodeNullBulk();
std::string encodeArray(const std::vector<std::string>& items);
std::string encodeNullArray();
