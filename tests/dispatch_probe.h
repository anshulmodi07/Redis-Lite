#pragma once

#include "client.h"
#include "commands.h"
#include "parser.h"

#include <vector>

inline std::string dispatchProbe(
    std::vector<RedisDb>& databases,
    const std::vector<std::string>& argv,
    int db_index = 0)
{
    static bool ready = false;
    if (!ready)
    {
        initCommandTable();
        ready = true;
    }

    if (databases.empty())
    {
        databases.resize(1);
    }

    Client client;
    client.db_index = db_index;
    return dispatch(client, databases, argv);
}
