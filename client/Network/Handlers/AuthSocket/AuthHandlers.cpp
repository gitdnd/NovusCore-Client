#include "AuthHandlers.h"
#include <Networking/NetPacket.h>
#include <Networking/NetClient.h>
#include <Networking/NetPacketHandler.h>
#include <Utils/ByteBuffer.h>
#include "../../../Utils/ServiceLocator.h"
#include "../../../ECS/Components/Network/AuthenticationSingleton.h"
#include "../../../ECS/Systems/Network/ConnectionSystems.h"

// @TODO: Remove Temporary Includes when they're no longer needed
#include <Utils/DebugHandler.h>

namespace AuthSocket
{
    void AuthHandlers::Setup(NetPacketHandler* netPacketHandler)
    {
        netPacketHandler->SetMessageHandler(Opcode::SMSG_LOGON_CHALLENGE, { ConnectionStatus::AUTH_CHALLENGE, sizeof(ServerLogonChallenge), AuthHandlers::HandshakeHandler });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_LOGON_HANDSHAKE, { ConnectionStatus::AUTH_HANDSHAKE, sizeof(ServerLogonHandshake), AuthHandlers::HandshakeResponseHandler });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_SEND_ADDRESS, { ConnectionStatus::AUTH_NONE, sizeof(u8), sizeof(u8) + sizeof(u32) + sizeof(u16), AuthHandlers::HandleSendAddress });
    }
    bool AuthHandlers::HandshakeHandler(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        ServerLogonChallenge logonChallenge;
        logonChallenge.Deserialize(packet->payload);

        entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
        AuthenticationSingleton& authenticationSingleton = gameRegistry->ctx<AuthenticationSingleton>();

        // If "ProcessChallenge" fails, we have either hit a bad memory allocation or a SRP-6a safety check, thus we should close the connection
        if (!authenticationSingleton.srp.ProcessChallenge(logonChallenge.s, logonChallenge.B))
            return false;

        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<36>();
        ClientLogonHandshake clientResponse;

        std::memcpy(clientResponse.M1, authenticationSingleton.srp.M, 32);

        buffer->Put(Opcode::CMSG_LOGON_HANDSHAKE);
        buffer->PutU16(0);

        u16 payloadSize = clientResponse.Serialize(buffer);
        buffer->Put<u16>(payloadSize, 2);
        netClient->Send(buffer);

        netClient->SetConnectionStatus(ConnectionStatus::AUTH_HANDSHAKE);
        return true;
    }
    bool AuthHandlers::HandshakeResponseHandler(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        // Handle handshake response
        ServerLogonHandshake logonResponse;
        logonResponse.Deserialize(packet->payload);

        entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
        AuthenticationSingleton& authenticationSingleton = gameRegistry->ctx<AuthenticationSingleton>();

        if (!authenticationSingleton.srp.VerifySession(logonResponse.HAMK))
        {
            DebugHandler::PrintWarning("Unsuccessful Login");
            return false;
        }
        else
        {
            DebugHandler::PrintSuccess("Successful Login");
        }

        // Send CMSG_CONNECTED (This will be changed in the future)
        std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<128>();
        buffer->Put(Opcode::CMSG_CONNECTED);
        buffer->PutU16(0);
        netClient->Send(buffer);

        netClient->SetConnectionStatus(ConnectionStatus::AUTH_SUCCESS);
        return true;
    }
    bool AuthHandlers::HandleSendAddress(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        u8 status = 0;
        u32 address = 0;
        u16 port = 0;

        if (!packet->payload->GetU8(status))
            return false;

        if (status > 0)
        {
            if (!packet->payload->GetU32(address))
                return false;

            if (!packet->payload->GetU16(port))
                return false;

            netClient->Close();

            if (netClient->Connect(address, port))
            {
                entt::registry* gameRegistry = ServiceLocator::GetGameRegistry();
                AuthenticationSingleton& authentication = gameRegistry->ctx<AuthenticationSingleton>();

                // Send Initial Packet

                std::shared_ptr<Bytebuffer> buffer = Bytebuffer::Borrow<512>();

                ClientLogonChallenge logonChallenge;
                logonChallenge.majorVersion = 3;
                logonChallenge.patchVersion = 3;
                logonChallenge.minorVersion = 5;
                logonChallenge.buildType = BuildType::Internal;
                logonChallenge.gameBuild = 12340;
                logonChallenge.gameName = "WoW";
                logonChallenge.username = "test";

                // Fetch Username & Password dynamically
                // We should probably also "hash" the password prior to using it to generate an account
                // that way we can immediately hash the password on the client and not have to worry
                // about any malicious attackers watching the memory
                authentication.username = "test";
                authentication.srp.username = "test";
                authentication.srp.password = "test";

                // If StartAuthentication fails, it means A failed to generate and thus we cannot connect
                if (!authentication.srp.StartAuthentication())
                    return false;

                buffer->Put(Opcode::CMSG_LOGON_CHALLENGE);
                buffer->PutU16(0);

                u16 payloadSize = logonChallenge.Serialize(buffer, authentication.srp.aBuffer);

                buffer->Put<u16>(payloadSize, 2);
                netClient->Send(buffer);

                netClient->SetConnectionStatus(ConnectionStatus::AUTH_CHALLENGE);
                return true;
            }
        }

        netClient->SetConnectionStatus(ConnectionStatus::AUTH_NONE);
        return true;
    }
}