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
    size_t head_ = 0;
};

std::string encodeSimpleString(const std::string& value);
const std::string& encodeOK();
const std::string& encodeError(const std::string& msg);
std::string encodeInteger(long long value);
void encodeIntegerInto(long long value, std::string& out);
std::string encodeBulkString(const std::string& value);
void encodeBulkStringInto(const std::string& value, std::string& out);
const std::string& encodeNullBulk();
std::string encodeArray(const std::vector<std::string>& items);
void encodeArrayInto(const std::vector<std::string>& items, std::string& out);
std::string encodeRespArray(const std::vector<std::string>& replies);
const std::string& encodeNullArray();

namespace Resp
{
void init();
extern const std::string OK;
extern const std::string PONG;
extern const std::string NULL_BULK;
extern const std::string INT_0;
extern const std::string INT_1;
extern const std::string QUEUED;
extern const std::string NULL_ARRAY;
extern std::string INTS[10000];
}
