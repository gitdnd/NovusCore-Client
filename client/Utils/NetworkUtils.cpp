#include "NetworkUtils.h"
#include "../ECS/Components/Network/AuthenticationSingleton.h"
#include "../ECS/Components/Network/ConnectionSingleton.h"
#include "../ECS/Systems/Network/ConnectionSystems.h"

namespace NetworkUtils
{
    void InitNetwork(entt::registry* registry)
    {
        ConnectionSingleton& connectionSingleton = registry->set<ConnectionSingleton>();
        AuthenticationSingleton& authenticationSingleton = registry->set<AuthenticationSingleton>();

        // Init Auth Socket
        {
            connectionSingleton.authConnection = std::make_shared<NetClient>();
            connectionSingleton.authConnection->Init(NetSocket::Mode::TCP);
        }

        // Init Game Socket
        {
            connectionSingleton.gameConnection = std::make_shared<NetClient>();
            connectionSingleton.gameConnection->Init(NetSocket::Mode::TCP);
        }
        
    }
    void DeInitNetwork(entt::registry* registry)
    {
        ConnectionSingleton& connectionSingleton = registry->ctx<ConnectionSingleton>();
        if (connectionSingleton.authConnection->IsConnected())
        {
            connectionSingleton.authConnection->Close();
        }

        if (connectionSingleton.gameConnection->IsConnected())
        {
            connectionSingleton.gameConnection->Close();
        }
    }
}
