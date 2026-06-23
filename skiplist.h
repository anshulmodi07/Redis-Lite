#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct ZSetEntry
{
    std::string member;
    double score;
};

class SkipList
{
public:
    bool insert(double score, const std::string& member);
    bool remove(const std::string& member);
    bool score(const std::string& member, double& out) const;
    long long rank(const std::string& member, bool reverse) const;
    std::vector<ZSetEntry> rangeByRank(long long start, long long stop, bool reverse) const;
    std::vector<ZSetEntry> rangeByScore(double min, double max, bool reverse, long long offset, long long count) const;
    size_t countByScore(double min, double max) const;
    size_t size() const;

private:
    std::set<std::pair<double, std::string>> ordered_;
    std::unordered_map<std::string, double> scores_;
};

struct ZSet
{
    std::unordered_map<std::string, double> scores;
    SkipList index;
};
