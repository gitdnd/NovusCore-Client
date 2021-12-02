#include "ConnectionSystems.h"
#include <entt.hpp>
#include <tracy/Tracy.hpp>
#include <Networking/NetClient.h>
#include <Networking/NetPacketHandler.h>
#include "../../Components/Network/ConnectionSingleton.h"
#include "../../Components/Network/AuthenticationSingleton.h"
#include "../../../Utils/ServiceLocator.h"

void ConnectionUpdateSystem::Update(entt::registry& registry)
{
    ZoneScopedNC("ConnectionUpdateSystem::Update", tracy::Color::Blue)
    ConnectionSingleton& connectionSingleton = registry.ctx<ConnectionSingleton>();

    if (connectionSingleton.authConnection)
    {
        if (connectionSingleton.authConnection->Read())
        {
            AuthSocket_HandleRead(connectionSingleton.authConnection);
        }

        if (!connectionSingleton.authConnection->IsConnected())
        {
            if (!connectionSingleton.authDidHandleDisconnect)
            {
                connectionSingleton.authDidHandleDisconnect = true;

                AuthSocket_HandleDisconnect(connectionSingleton.authConnection);
            }
        }
        else
        {
            std::shared_ptr<NetPacket> packet = nullptr;

            NetPacketHandler* authNetPacketHandler = ServiceLocator::GetAuthNetPacketHandler();
            while (connectionSingleton.authPacketQueue.try_dequeue(packet))
            {
#ifdef NC_Debug
                DebugHandler::PrintSuccess("[Network/Socket]: CMD: %u, Size: %u", packet->header.opcode, packet->header.size);
#endif // NC_Debug

                if (!authNetPacketHandler->CallHandler(connectionSingleton.authConnection, packet))
                {
                    connectionSingleton.authConnection->Close();
                    connectionSingleton.authConnection = nullptr;
                    return;
                }
            }
        }
    }

    if (connectionSingleton.gameConnection)
    {
        if (connectionSingleton.gameConnection->Read())
        {
            GameSocket_HandleRead(connectionSingleton.gameConnection);
        }

        if (!connectionSingleton.gameConnection->IsConnected())
        {
            if (!connectionSingleton.gameDidHandleDisconnect)
            {
                connectionSingleton.gameDidHandleDisconnect = true;

                GameSocket_HandleDisconnect(connectionSingleton.gameConnection);
            }
        }
        else
        {
            std::shared_ptr<NetPacket> packet = nullptr;

            NetPacketHandler* gameNetPacketHandler = ServiceLocator::GetGameNetPacketHandler();
            while (connectionSingleton.gamePacketQueue.try_dequeue(packet))
            {
#ifdef NC_Debug
                DebugHandler::PrintSuccess("[Network/Socket]: CMD: %u, Size: %u", packet->header.opcode, packet->header.size);
#endif // NC_Debug

                if (!gameNetPacketHandler->CallHandler(connectionSingleton.gameConnection, packet))
                {
                    connectionSingleton.gameConnection->Close();
                    connectionSingleton.gameConnection = nullptr;
                    return;
                }
            }
        }
    }
}

void ConnectionUpdateSystem::AuthSocket_HandleConnect(std::shared_ptr<NetClient> netClient, bool connected)
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    ConnectionSingleton& connectionSingleton = registry->ctx<ConnectionSingleton>();
    connectionSingleton.authDidHandleDisconnect = !connected;
    // The client initially will connect to a region server, from there on the client receives
    // an IP address / port from that region server to the proper authentication server.

    if (connected)
    {
        std::shared_ptr<NetSocket> socket = netClient->GetSocket();
        socket->SetBlockingState(false);
        socket->SetSendBufferSize(8192);
        socket->SetReceiveBufferSize(8192);
        socket->SetNoDelayState(true);

        /* Send Initial Packet */
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<512>();
        buffer->Put(Opcode::MSG_REQUEST_ADDRESS);
        buffer->PutU16(0);
        netClient->Send(buffer);
    }
}
void ConnectionUpdateSystem::AuthSocket_HandleRead(std::shared_ptr<NetClient> netClient)
{
    entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
    std::shared_ptr<Bytebuffer> buffer = netClient->GetReadBuffer();

    ConnectionSingleton* connectionSingleton = &gameRegistry->ctx<ConnectionSingleton>();

    while (size_t activeSize = buffer->GetActiveSize())
    {
        // We have received a partial header and need to read more
        if (activeSize < sizeof(PacketHeader))
        {
            buffer->Normalize();
            break;
        }

        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer->GetReadPointer());

        if (header->opcode == Opcode::INVALID || header->opcode > Opcode::MAX_COUNT)
        {
#ifdef NC_Debug
            DebugHandler::PrintError("Received Invalid Opcode (%u) from network stream", static_cast<u16>(header->opcode));
#endif // NC_Debug
            break;
        }

        if (header->size > 8192)
        {
#ifdef NC_Debug
            DebugHandler::PrintError("Received Invalid Opcode Size (%u) from network stream", header->size);
#endif // NC_Debug
            break;
        }

        size_t sizeWithoutHeader = activeSize - sizeof(PacketHeader);

        // We have received a valid header, but we have yet to receive the entire payload
        if (sizeWithoutHeader < header->size)
        {
            buffer->Normalize();
            break;
        }

        // Skip Header
        buffer->SkipRead(sizeof(PacketHeader));

        std::shared_ptr<NetPacket> packet = NetPacket::Borrow();
        {
            // Header
            {
                packet->header = *header;
            }

            // Payload
            {
                if (packet->header.size)
                {
                    packet->payload = Bytebuffer::Borrow<8192/*NETWORK_BUFFER_SIZE*/ >();
                    packet->payload->size = packet->header.size;
                    packet->payload->writtenData = packet->header.size;
                    std::memcpy(packet->payload->GetDataPointer(), buffer->GetReadPointer(), packet->header.size);

                    // Skip Payload
                    buffer->SkipRead(header->size);
                }
            }

            connectionSingleton->authPacketQueue.enqueue(packet);
        }
    }

    // Only reset if we read everything that was written
    if (buffer->GetActiveSize() == 0)
    {
        buffer->Reset();
    }
}
void ConnectionUpdateSystem::AuthSocket_HandleDisconnect(std::shared_ptr<NetClient> netClient)
{
    DebugHandler::PrintWarning("Disconnected from AuthSocket");
}

void ConnectionUpdateSystem::GameSocket_HandleConnect(std::shared_ptr<NetClient> netClient, bool connected)
{
    entt::registry* registry = ServiceLocator::GetGameRegistry();
    ConnectionSingleton& connectionSingleton = registry->ctx<ConnectionSingleton>();
    connectionSingleton.gameDidHandleDisconnect = !connected;

    if (connected)
    {
        std::shared_ptr<NetSocket> socket = netClient->GetSocket();
        socket->SetBlockingState(false);
        socket->SetSendBufferSize(8192);
        socket->SetReceiveBufferSize(8192);
        socket->SetNoDelayState(true);

        entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
        AuthenticationSingleton& authentication = gameRegistry->ctx<AuthenticationSingleton>();
        ConnectionSingleton& connectionSingleton = gameRegistry->ctx<ConnectionSingleton>();

        /* Send Initial Packet */
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<512>();
        ClientLogonChallenge logonChallenge;
        logonChallenge.majorVersion = 3;
        logonChallenge.patchVersion = 3;
        logonChallenge.minorVersion = 5;
        logonChallenge.buildType = BuildType::Internal;
        logonChallenge.gameBuild = 12340;
        logonChallenge.gameName = "WoW";
        logonChallenge.username = "test";

        authentication.username = "test";
        authentication.srp.username = "test";
        authentication.srp.password = "test";

        // If StartAuthentication fails, it means A failed to generate and thus we cannot connect
        if (!authentication.srp.StartAuthentication())
            return;

        buffer->Put(Opcode::CMSG_LOGON_CHALLENGE);
        buffer->PutU16(0);

        u16 payloadSize = logonChallenge.Serialize(buffer, authentication.srp.aBuffer);

        buffer->Put<u16>(payloadSize, 2);
        netClient->Send(buffer);

        connectionSingleton.gameConnection->SetConnectionStatus(ConnectionStatus::AUTH_CHALLENGE);
    }
}
void ConnectionUpdateSystem::GameSocket_HandleRead(std::shared_ptr<NetClient> netClient)
{
    entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
    std::shared_ptr<Bytebuffer> buffer = netClient->GetReadBuffer();

    ConnectionSingleton* connectionSingleton = &gameRegistry->ctx<ConnectionSingleton>();

    while (size_t activeSize = buffer->GetActiveSize())
    {
        // We have received a partial header and need to read more
        if (activeSize < sizeof(PacketHeader))
        {
            buffer->Normalize();
            break;
        }

        PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer->GetReadPointer());

        if (header->opcode == Opcode::INVALID || header->opcode > Opcode::MAX_COUNT)
        {
#ifdef NC_Debug
            DebugHandler::PrintError("Received Invalid Opcode (%u) from network stream", static_cast<u16>(header->opcode));
#endif // NC_Debug
            break;
        }

        if (header->size > 8192)
        {
#ifdef NC_Debug
            DebugHandler::PrintError("Received Invalid Opcode Size (%u) from network stream", header->size);
#endif // NC_Debug
            break;
        }

        size_t sizeWithoutHeader = activeSize - sizeof(PacketHeader);

        // We have received a valid header, but we have yet to receive the entire payload
        if (sizeWithoutHeader < header->size)
        {
            buffer->Normalize();
            break;
        }

        // Skip Header
        buffer->SkipRead(sizeof(PacketHeader));

        std::shared_ptr<NetPacket> packet = NetPacket::Borrow();
        {
            // Header
            {
                packet->header = *header;
            }

            // Payload
            {
                if (packet->header.size)
                {
                    packet->payload = Bytebuffer::Borrow<8192/*NETWORK_BUFFER_SIZE*/ >();
                    packet->payload->size = packet->header.size;
                    packet->payload->writtenData = packet->header.size;
                    std::memcpy(packet->payload->GetDataPointer(), buffer->GetReadPointer(), packet->header.size);

                    // Skip Payload
                    buffer->SkipRead(header->size);
                }
            }

            connectionSingleton->gamePacketQueue.enqueue(packet);
        }
    }

    // Only reset if we read everything that was written
    if (buffer->GetActiveSize() == 0)
    {
        buffer->Reset();
    }
}
void ConnectionUpdateSystem::GameSocket_HandleDisconnect(std::shared_ptr<NetClient> netClient)
{
    DebugHandler::PrintWarning("Disconnected from GameSocket");
}