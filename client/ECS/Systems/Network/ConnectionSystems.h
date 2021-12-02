#pragma once
#include <entity/fwd.hpp>

class NetClient;
class ConnectionUpdateSystem
{
public:
    static void Update(entt::registry& registry);

    // Handlers for Network Client
    static void AuthSocket_HandleConnect(std::shared_ptr<NetClient> netClient, bool connected);
    static void AuthSocket_HandleRead(std::shared_ptr<NetClient> netClient);
    static void AuthSocket_HandleDisconnect(std::shared_ptr<NetClient> netClient);
    static void GameSocket_HandleConnect(std::shared_ptr<NetClient> netClient, bool connected);
    static void GameSocket_HandleRead(std::shared_ptr<NetClient> netClient);
    static void GameSocket_HandleDisconnect(std::shared_ptr<NetClient> netClient);
};