#include "eviction.h"

#include <cstring>
#include <iostream>
#include <random>
#include <sstream>

using namespace std;

namespace
{
bool parseSlotRange(const string& text, uint16_t& start, uint16_t& end)
{
    const size_t dash = text.find('-');
    if (dash == string::npos)
    {
        try
        {
            const long long slot = stoll(text);
            if (slot < 0 || slot > 16383)
            {
                return false;
            }

            start = end = static_cast<uint16_t>(slot);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    try
    {
        start = static_cast<uint16_t>(stoll(text.substr(0, dash)));
        end = static_cast<uint16_t>(stoll(text.substr(dash + 1)));
        return start <= end && end <= 16383;
    }
    catch (...)
    {
        return false;
    }
}

string randomClusterId()
{
    static mt19937_64 rng(random_device{}());
    ostringstream out;
    out << hex;
    for (int i = 0; i < 40; ++i)
    {
        out << (rng() % 16);
    }

    return out.str();
}
}

bool parseServerArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
        {
            g_server_config.port = stoi(argv[++i]);
        }
        else if (arg == "--replicaof" && i + 2 < argc)
        {
            g_server_config.replicaof_host = argv[++i];
            g_server_config.replicaof_port = stoi(argv[++i]);
            g_server_config.readonly_replica = true;
        }
        else if (arg == "--cluster-enabled")
        {
            g_server_config.cluster_enabled = true;
        }
        else if (arg == "--cluster-id" && i + 1 < argc)
        {
            g_server_config.cluster_id = argv[++i];
        }
        else if (arg == "--cluster-announce-ip" && i + 1 < argc)
        {
            g_server_config.cluster_announce_ip = argv[++i];
        }
        else if (arg == "--cluster-bus-port" && i + 1 < argc)
        {
            g_server_config.cluster_bus_port = static_cast<uint16_t>(stoi(argv[++i]));
        }
        else if (arg == "--cluster-slots" && i + 1 < argc)
        {
            uint16_t start = 0;
            uint16_t end = 0;
            if (!parseSlotRange(argv[++i], start, end))
            {
                cerr << "Invalid --cluster-slots range\n";
                return false;
            }

            g_server_config.cluster_slots.emplace_back(start, end);
        }
        else
        {
            cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (g_server_config.cluster_enabled)
    {
        if (g_server_config.cluster_id.empty())
        {
            g_server_config.cluster_id = randomClusterId();
        }

        if (g_server_config.cluster_bus_port == 0)
        {
            g_server_config.cluster_bus_port = static_cast<uint16_t>(g_server_config.port + 10000);
        }

        if (g_server_config.cluster_slots.empty())
        {
            g_server_config.cluster_slots.emplace_back(0, 16383);
        }
    }

    return true;
}
