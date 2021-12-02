#pragma once
#include <memory>

class NetPacketHandler;
struct NetPacket;
class NetClient;
namespace AuthSocket
{
    class AuthHandlers
    {
    public:
        static void Setup(NetPacketHandler*);
        static bool HandleSendAddress(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandshakeHandler(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
        static bool HandshakeResponseHandler(std::shared_ptr<NetClient>, std::shared_ptr<NetPacket>);
    };
}