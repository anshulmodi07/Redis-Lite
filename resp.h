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
