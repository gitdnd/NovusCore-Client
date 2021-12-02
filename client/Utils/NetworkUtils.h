#pragma once
#include <entity/fwd.hpp>
#include <asio/io_service.hpp>

struct ConnectionSingleton;
namespace NetworkUtils
{
    void InitNetwork(entt::registry* registry);
    void DeInitNetwork(entt::registry* registry);
}