#include "resp.h"

#include "parser.h"

#include <cctype>
#include <stdexcept>
#include <unordered_map>

using namespace std;

namespace
{
bool readLineAt(const string& buffer, size_t pos, string& line, size_t& next)
{
    size_t end = buffer.find("\r\n", pos);
    size_t delimiter_size = 2;

    if (end == string::npos)
    {
        end = buffer.find('\n', pos);
        delimiter_size = 1;
    }

    if (end == string::npos)
    {
        return false;
    }

    line = buffer.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }

    next = end + delimiter_size;
    return true;
}

long long parseInteger(const string& value, const string& context)
{
    if (value.empty())
    {
        throw invalid_argument("protocol error: empty " + context);
    }

    size_t pos = 0;
    bool negative = false;

    if (value[pos] == '-')
    {
        negative = true;
        ++pos;
    }

    if (pos == value.size())
    {
        throw invalid_argument("protocol error: invalid " + context);
    }

    long long result = 0;
    for (; pos < value.size(); ++pos)
    {
        unsigned char ch = static_cast<unsigned char>(value[pos]);
        if (!isdigit(ch))
        {
            throw invalid_argument("protocol error: invalid " + context);
        }

        result = (result * 10) + (value[pos] - '0');
    }

    return negative ? -result : result;
}

bool parseBulkStringAt(
    const string& buffer,
    size_t pos,
    string& out,
    size_t& next)
{
    string header;
    if (!readLineAt(buffer, pos, header, next))
    {
        return false;
    }

    if (header.empty() || header[0] != '$')
    {
        throw invalid_argument("protocol error: expected bulk string");
    }

    long long len = parseInteger(header.substr(1), "bulk length");
    if (len < 0)
    {
        throw invalid_argument("protocol error: null bulk string is not a command argument");
    }

    size_t bulk_len = static_cast<size_t>(len);
    if (buffer.size() < next + bulk_len + 2)
    {
        return false;
    }

    out.assign(buffer, next, bulk_len);
    next += bulk_len;

    if (buffer[next] != '\r' || buffer[next + 1] != '\n')
    {
        throw invalid_argument("protocol error: bulk string missing CRLF terminator");
    }

    next += 2;
    return true;
}

bool parseArrayAt(
    const string& buffer,
    size_t pos,
    vector<string>& out,
    size_t& next)
{
    string header;
    if (!readLineAt(buffer, pos, header, next))
    {
        return false;
    }

    if (header.empty() || header[0] != '*')
    {
        throw invalid_argument("protocol error: expected array");
    }

    long long count = parseInteger(header.substr(1), "array length");
    if (count < 0)
    {
        throw invalid_argument("protocol error: null array is not a command");
    }

    out.clear();
    out.resize(static_cast<size_t>(count));

    for (long long i = 0; i < count; ++i)
    {
        size_t after_bulk = next;

        if (!parseBulkStringAt(buffer, next, out[static_cast<size_t>(i)], after_bulk))
        {
            return false;
        }

        next = after_bulk;
    }

    return true;
}

bool parseInlineAt(
    const string& buffer,
    size_t pos,
    vector<string>& out,
    size_t& next)
{
    string line;
    if (!readLineAt(buffer, pos, line, next))
    {
        return false;
    }

    tokenize(line, out);
    return true;
}
}

void RespParser::feed(const char* data, size_t len)
{
    buffer_.append(data, len);
}

bool RespParser::tryParse(vector<string>& out)
{
    if (buffer_.empty() || head_ == buffer_.size())
    {
        return false;
    }

    out.clear();
    size_t next = head_;
    bool complete = false;

    if (buffer_[head_] == '*')
    {
        complete = parseArrayAt(buffer_, head_, out, next);
    }
    else
    {
        complete = parseInlineAt(buffer_, head_, out, next);
    }

    if (!complete)
    {
        return false;
    }

    head_ = next;
    if (head_ == buffer_.size())
    {
        buffer_.clear();
        head_ = 0;
    }
    else if (head_ > 65536 && head_ > buffer_.size() / 2)
    {
        buffer_.erase(0, head_);
        head_ = 0;
    }

    return true;
}

size_t RespParser::bufferedSize() const
{
    return buffer_.size() - head_;
}

string encodeSimpleString(const string& value)
{
    return "+" + value + "\r\n";
}

const string& encodeOK()
{
    return Resp::OK;
}

namespace
{
const string& cachedErrorResponse(const string& msg)
{
    static unordered_map<string, string> cache;
    auto it = cache.find(msg);
    if (it != cache.end())
    {
        return it->second;
    }

    auto inserted = cache.emplace(msg, "-" + msg + "\r\n");
    return inserted.first->second;
}
}

const string& encodeError(const string& msg)
{
    return cachedErrorResponse(msg);
}

void encodeIntegerInto(long long value, string& out)
{
    if (value >= 0 && value < 10000)
    {
        out += Resp::INTS[value];
        return;
    }
    out += ':';
    out += to_string(value);
    out += "\r\n";
}

string encodeInteger(long long value)
{
    if (value >= 0 && value < 10000)
    {
        return Resp::INTS[value];
    }
    string res;
    encodeIntegerInto(value, res);
    return res;
}

void encodeBulkStringInto(const string& value, string& out)
{
    out.reserve(out.size() + 1 + 20 + 2 + value.size() + 2);
    out += '$';
    out += to_string(value.size());
    out += "\r\n";
    out += value;
    out += "\r\n";
}

string encodeBulkString(const string& value)
{
    string response;
    encodeBulkStringInto(value, response);
    return response;
}

const string& encodeNullBulk()
{
    return Resp::NULL_BULK;
}

void encodeArrayInto(const vector<string>& items, string& out)
{
    out += '*';
    out += to_string(items.size());
    out += "\r\n";

    for (const string& item : items)
    {
        encodeBulkStringInto(item, out);
    }
}

string encodeArray(const vector<string>& items)
{
    string response;
    encodeArrayInto(items, response);
    return response;
}

string encodeRespArray(const vector<string>& replies)
{
    string response = "*" + to_string(replies.size()) + "\r\n";
    for (const string& reply : replies)
    {
        response += reply;
    }

    return response;
}

const string& encodeNullArray()
{
    return Resp::NULL_ARRAY;
}

namespace Resp
{
const std::string OK = "+OK\r\n";
const std::string PONG = "+PONG\r\n";
const std::string NULL_BULK = "$-1\r\n";
const std::string INT_0 = ":0\r\n";
const std::string INT_1 = ":1\r\n";
const std::string QUEUED = "+QUEUED\r\n";
const std::string NULL_ARRAY = "*-1\r\n";
std::string INTS[10000];

void init()
{
    for (int i = 0; i < 10000; ++i)
    {
        INTS[i] = ":" + to_string(i) + "\r\n";
    }
}
}
