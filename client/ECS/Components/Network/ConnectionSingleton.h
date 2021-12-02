#pragma once
#include <NovusTypes.h>
#include <Utils/ConcurrentQueue.h>
#include <Networking/NetPacket.h>
#include <Networking/NetClient.h>

struct ConnectionSingleton
{
public:
    ConnectionSingleton() : authPacketQueue(256), gamePacketQueue(256) { }

    std::shared_ptr<NetClient> authConnection;
    bool authDidHandleDisconnect = true;

    std::shared_ptr<NetClient> gameConnection;
    bool gameDidHandleDisconnect = true;

    moodycamel::ConcurrentQueue<std::shared_ptr<NetPacket>> authPacketQueue;
    moodycamel::ConcurrentQueue<std::shared_ptr<NetPacket>> gamePacketQueue;
};