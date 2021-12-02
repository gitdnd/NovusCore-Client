#pragma once
#include <entity/fwd.hpp>

struct ConnectionSingleton;
namespace NetworkUtils
{
    void InitNetwork(entt::registry* registry);
    void DeInitNetwork(entt::registry* registry);
}