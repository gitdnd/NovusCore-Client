#pragma once
#include <memory>

class NetPacketHandler;
struct NetPacket;
class NetClient;
namespace GameSocket
{
    class GameHandlers
    {
    public:
        static void Setup(NetPacketHandler*);
        static bool HandshakeHandler(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandshakeResponseHandler(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandleConnected(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandleCreatePlayer(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandleCreateEntity(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandleUpdateEntity(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandleDeleteEntity(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);

        static std::vector<u32> receivedEntityIDs;
    };
}