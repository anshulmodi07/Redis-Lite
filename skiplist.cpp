#include "skiplist.h"

#include <algorithm>

using namespace std;

bool SkipList::insert(double score, const string& member)
{
    auto it = scores_.find(member);
    bool is_new = it == scores_.end();
    if (!is_new)
    {
        ordered_.erase({it->second, member});
    }

    scores_[member] = score;
    ordered_.insert({score, member});
    return is_new;
}

bool SkipList::remove(const string& member)
{
    auto it = scores_.find(member);
    if (it == scores_.end())
    {
        return false;
    }

    ordered_.erase({it->second, member});
    scores_.erase(it);
    return true;
}

bool SkipList::score(const string& member, double& out) const
{
    auto it = scores_.find(member);
    if (it == scores_.end())
    {
        return false;
    }

    out = it->second;
    return true;
}

long long SkipList::rank(const string& member, bool reverse) const
{
    auto score_it = scores_.find(member);
    if (score_it == scores_.end())
    {
        return -1;
    }

    long long pos = 0;
    for (const auto& entry : ordered_)
    {
        if (entry.second == member)
        {
            return reverse ? static_cast<long long>(ordered_.size()) - pos - 1 : pos;
        }
        ++pos;
    }

    return -1;
}

vector<ZSetEntry> SkipList::rangeByRank(long long start, long long stop, bool reverse) const
{
    vector<ZSetEntry> result;
    long long n = static_cast<long long>(ordered_.size());
    if (start < 0)
    {
        start += n;
    }
    if (stop < 0)
    {
        stop += n;
    }

    start = max<long long>(start, 0);
    stop = min<long long>(stop, n - 1);
    if (n == 0 || start > stop || start >= n)
    {
        return result;
    }

    long long pos = 0;
    if (!reverse)
    {
        for (const auto& entry : ordered_)
        {
            if (pos >= start && pos <= stop)
            {
                result.push_back({entry.second, entry.first});
            }
            ++pos;
        }
        return result;
    }

    for (auto it = ordered_.rbegin(); it != ordered_.rend(); ++it)
    {
        if (pos >= start && pos <= stop)
        {
            result.push_back({it->second, it->first});
        }
        ++pos;
    }
    return result;
}

vector<ZSetEntry> SkipList::rangeByScore(double min, double max, bool reverse, long long offset, long long count) const
{
    vector<ZSetEntry> result;
    if (offset < 0 || count == 0 || min > max)
    {
        return result;
    }

    long long skipped = 0;
    auto add = [&](const pair<double, string>& entry) {
        if (entry.first < min || entry.first > max)
        {
            return;
        }
        if (skipped++ < offset)
        {
            return;
        }
        if (count >= 0 && static_cast<long long>(result.size()) >= count)
        {
            return;
        }
        result.push_back({entry.second, entry.first});
    };

    if (!reverse)
    {
        for (auto it = ordered_.lower_bound({min, ""}); it != ordered_.end(); ++it)
        {
            if (it->first > max || (count >= 0 && static_cast<long long>(result.size()) >= count))
            {
                break;
            }
            add(*it);
        }
        return result;
    }

    for (auto it = ordered_.rbegin(); it != ordered_.rend(); ++it)
    {
        if (it->first < min || (count >= 0 && static_cast<long long>(result.size()) >= count))
        {
            break;
        }
        add(*it);
    }
    return result;
}

size_t SkipList::countByScore(double min, double max) const
{
    return rangeByScore(min, max, false, 0, -1).size();
}

size_t SkipList::size() const
{
    return ordered_.size();
}
