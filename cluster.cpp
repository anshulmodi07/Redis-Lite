#include "cluster.h"

#include "parser.h"

#include "resp.h"
#include "eviction.h"
#include "sha1.h"

#include <arpa/inet.h>
#include <cctype>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

using namespace std;

namespace
{
static const uint16_t crc16tab[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0};

struct RemoteNode
{
    string id;
    string ip;
    int port = 0;
    vector<pair<uint16_t, uint16_t>> slots;
};

unordered_map<string, RemoteNode> remote_nodes;
uint16_t slot_owner_id[16384];

string hashTag(const string& key)
{
    const size_t start = key.find('{');
    if (start == string::npos)
    {
        return key;
    }

    const size_t end = key.find('}', start + 1);
    if (end == string::npos || end == start + 1)
    {
        return key;
    }

    return key.substr(start + 1, end - start - 1);
}

uint16_t crc16(const string& text)
{
    uint16_t crc = 0;
    for (unsigned char ch : text)
    {
        crc = static_cast<uint16_t>((crc << 8) ^ crc16tab[((crc >> 8) ^ ch) & 0xFF]);
    }

    return crc;
}

void rebuildSlotOwners()
{
    for (uint16_t& owner : slot_owner_id)
    {
        owner = 0xFFFF;
    }

    for (const auto& range : g_server_config.cluster_slots)
    {
        for (uint16_t slot = range.first; slot <= range.second; ++slot)
        {
            slot_owner_id[slot] = 0;
        }
    }

    for (const auto& entry : remote_nodes)
    {
        uint16_t marker = 1;
        for (uint16_t slot = 0; slot < 16384; ++slot)
        {
            if (slot_owner_id[slot] == 0xFFFF)
            {
                slot_owner_id[slot] = marker;
                break;
            }
        }

        (void)entry;
    }
}

bool slotInLocalRanges(uint16_t slot)
{
    for (const auto& range : g_server_config.cluster_slots)
    {
        if (slot >= range.first && slot <= range.second)
        {
            return true;
        }
    }

    return false;
}
}

uint16_t clusterKeySlot(const string& key)
{
    return static_cast<uint16_t>(crc16(hashTag(key)) & 16383);
}

bool clusterSlotIsLocal(uint16_t slot)
{
    return slotInLocalRanges(slot);
}

string clusterSlotEndpoint(uint16_t slot)
{
    if (slotInLocalRanges(slot))
    {
        return g_server_config.cluster_announce_ip + ":" + to_string(g_server_config.port);
    }

    size_t index = 0;
    for (const auto& entry : remote_nodes)
    {
        for (const auto& range : entry.second.slots)
        {
            if (slot >= range.first && slot <= range.second)
            {
                return entry.second.ip + ":" + to_string(entry.second.port);
            }
        }

        ++index;
    }

    return "127.0.0.1:0";
}

void initCluster()
{
    if (!g_server_config.cluster_enabled)
    {
        return;
    }

    rebuildSlotOwners();
}

string clusterPreflight(const vector<string>& argv)
{
    if (!g_server_config.cluster_enabled || argv.empty())
    {
        return {};
    }

    for (size_t pos : keyPositions(argv))
    {
        if (pos >= argv.size())
        {
            continue;
        }

        const uint16_t slot = clusterKeySlot(argv[pos]);
        if (!clusterSlotIsLocal(slot))
        {
            const string endpoint = clusterSlotEndpoint(slot);
            return encodeError("MOVED " + to_string(slot) + " " + endpoint);
        }
    }

    return {};
}

string clusterInfoSection()
{
    if (!g_server_config.cluster_enabled)
    {
        return "# Cluster\r\ncluster_enabled:0\r\n";
    }

    string section = "# Cluster\r\n";
    section += "cluster_enabled:1\r\n";
    section += "cluster_myid:" + g_server_config.cluster_id + "\r\n";
    section += "cluster_known_nodes:" + to_string(1 + remote_nodes.size()) + "\r\n";
    section += "cluster_size:1\r\n";
    return section;
}

void clusterPeriodic()
{
}

bool clusterHandleBus(int)
{
    return true;
}

namespace
{
string commandCluster(CommandContext&, const vector<string>& argv)
{
    if (argv.size() < 2)
    {
        return encodeError("ERR wrong number of arguments for 'CLUSTER' command");
    }

    string sub = argv[1];
    for (char& ch : sub)
    {
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    }

    if (sub == "MYID")
    {
        return encodeBulkString(g_server_config.cluster_id);
    }

    if (sub == "INFO")
    {
        return encodeBulkString(clusterInfoSection());
    }

    if (sub == "NODES")
    {
        ostringstream node;
        node << g_server_config.cluster_id << " "
             << g_server_config.cluster_announce_ip << ":"
             << g_server_config.port << "@"
             << g_server_config.cluster_bus_port << " myself,master - 0 0 0 connected";
        for (const auto& range : g_server_config.cluster_slots)
        {
            node << " " << range.first << "-" << range.second;
        }

        return encodeBulkString(node.str());
    }

    if (sub == "MEET" && argv.size() == 4)
    {
        RemoteNode node;
        node.id = sha1Hex(argv[2] + argv[3]);
        node.ip = argv[2];
        node.port = stoi(argv[3]);
        for (uint16_t slot = 0; slot < 16384; ++slot)
        {
            if (!slotInLocalRanges(slot))
            {
                if (node.slots.empty() || node.slots.back().second + 1 != slot)
                {
                    node.slots.emplace_back(slot, slot);
                }
                else
                {
                    node.slots.back().second = slot;
                }
            }
        }

        remote_nodes[node.id] = node;
        return encodeSimpleString("OK");
    }

    if (sub == "SETSLOT" && argv.size() >= 4)
    {
        return encodeOK();
    }

    return encodeError("ERR unknown subcommand or wrong number of arguments for 'CLUSTER'");
}
}

void registerClusterCommands(CommandTable& table)
{
    table["CLUSTER"] = Command{"CLUSTER", commandCluster, -2, CMD_READONLY};
}
