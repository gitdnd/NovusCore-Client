#include "GameHandlers.h"
#include <entt.hpp>
#include <Networking/NetPacket.h>
#include <Networking/NetClient.h>
#include <Networking/NetPacketHandler.h>
#include "../../../Utils/EntityUtils.h"
#include "../../../Utils/ServiceLocator.h"
#include "../../../Rendering/CameraFreelook.h"
#include "../../../Rendering/CameraOrbital.h"
#include "../../../Gameplay/GameConsole/GameConsole.h"

#include <Gameplay/ECS/Components/Transform.h>
#include <Gameplay/ECS/Components/GameEntity.h>
#include <Gameplay/ECS/Components/Movement.h>
#include "../../../ECS/Components/Singletons/LocalplayerSingleton.h"
#include "../../../ECS/Components/Network/AuthenticationSingleton.h"
#include "../../../ECS/Components/Rendering/VisibleModel.h"
#include "../../../ECS/Components/Rendering/ModelDisplayInfo.h"

namespace GameSocket
{
    std::vector<u32> GameHandlers::receivedEntityIDs;

    void GameHandlers::Setup(NetPacketHandler* netPacketHandler)
    {
        // Setup other handlers
        netPacketHandler->SetMessageHandler(Opcode::SMSG_LOGON_CHALLENGE, { ConnectionStatus::AUTH_CHALLENGE, sizeof(ServerLogonChallenge), GameHandlers::HandshakeHandler });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_LOGON_HANDSHAKE, { ConnectionStatus::AUTH_HANDSHAKE, sizeof(ServerLogonHandshake), GameHandlers::HandshakeResponseHandler });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_CONNECTED,       { ConnectionStatus::AUTH_SUCCESS, 0, GameHandlers::HandleConnected });

        netPacketHandler->SetMessageHandler(Opcode::SMSG_CREATE_PLAYER,   { ConnectionStatus::CONNECTED, static_cast<u16>(sizeof(entt::entity) + GameEntity::GetPacketSize() + Transform::GetPacketSize()), GameHandlers::HandleCreatePlayer});
        netPacketHandler->SetMessageHandler(Opcode::SMSG_CREATE_ENTITY,   { ConnectionStatus::CONNECTED, static_cast<u16>(sizeof(entt::entity) + GameEntity::GetPacketSize() + Transform::GetPacketSize()), GameHandlers::HandleCreateEntity });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_UPDATE_ENTITY,   { ConnectionStatus::CONNECTED, static_cast<u16>(sizeof(entt::entity) + Transform::GetPacketSize()), GameHandlers::HandleUpdateEntity });
        netPacketHandler->SetMessageHandler(Opcode::SMSG_DELETE_ENTITY,   { ConnectionStatus::CONNECTED, sizeof(entt::entity), GameHandlers::HandleDeleteEntity });
        
        netPacketHandler->SetMessageHandler(Opcode::SMSG_STORELOC,        { ConnectionStatus::CONNECTED, sizeof(u8) + 1, sizeof(u8) + 257 + sizeof(vec3) + sizeof(f32), GameHandlers::HandleStoreLocAck });
    }

    bool GameHandlers::HandshakeHandler(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
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
    bool GameHandlers::HandshakeResponseHandler(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
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
    bool GameHandlers::HandleConnected(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        netClient->SetConnectionStatus(ConnectionStatus::CONNECTED);
        return true;
    }
    bool GameHandlers::HandleCreatePlayer(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetGameRegistry();

        entt::entity entity = entt::null;
        u8 type;
        u32 displayID;

        packet->payload->Get(entity);
        packet->payload->GetU8(type);
        packet->payload->GetU32(displayID);

        // Create Localplayer
        {
            LocalplayerSingleton& localplayerSingleton = registry->ctx_or_set<LocalplayerSingleton>();

            // If the localplayer already has an entity, we need to destroy it first
            if (localplayerSingleton.entity != entt::null &&
                registry->valid(localplayerSingleton.entity))
            {
                registry->destroy(localplayerSingleton.entity);
            }
            localplayerSingleton.entity = registry->create(entity);

            Transform& transform = registry->emplace<Transform>(localplayerSingleton.entity);
            packet->payload->Deserialize(transform);

            registry->emplace<TransformIsDirty>(localplayerSingleton.entity);
            registry->emplace<Movement>(localplayerSingleton.entity);

            ModelDisplayInfo& modelDisplayInfo = registry->emplace<ModelDisplayInfo>(localplayerSingleton.entity, ModelType::Creature, displayID);

            if (ServiceLocator::GetCameraOrbital()->IsActive())
                registry->remove<VisibleModel>(localplayerSingleton.entity);
        }

        return true;
    }
    bool GameHandlers::HandleCreateEntity(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetGameRegistry();
        LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();

        u32 hintedEntityID = std::numeric_limits<u32>().max();
        u8 type;
        u32 displayID;

        packet->payload->GetU32(hintedEntityID);

        for (u32 i = 0; i < receivedEntityIDs.size(); i++)
        {
            if (hintedEntityID == receivedEntityIDs[i])
            {
                DebugHandler::PrintFatal("RECEIVED DUPLICATE ENTITY ID");
            }
        }

        receivedEntityIDs.push_back(hintedEntityID);

        //if (localplayerSingleton.entity == hintedEntity)
            //return true;

        packet->payload->GetU8(type);
        packet->payload->GetU32(displayID);

        entt::entity hintedEntity = entt::entity(hintedEntityID);

        // Create ENTT entity
        entt::entity entity = registry->create(hintedEntity);
        if (hintedEntity != entity)
        {
            DebugHandler::PrintFatal("DESYNC");
        }
        Transform& transform = registry->emplace<Transform>(entity);

        packet->payload->Get(transform.position);
        packet->payload->Get(transform.rotation);
        packet->payload->Get(transform.scale);

        registry->emplace<TransformIsDirty>(entity);
        registry->emplace<Movement>(entity);

        ModelDisplayInfo& modelDisplayInfo = registry->emplace<ModelDisplayInfo>(entity, ModelType::Creature, displayID);

        return true;
    }
    bool GameHandlers::HandleUpdateEntity(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetGameRegistry();
        LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();

        entt::entity entityId = entt::null;
        packet->payload->Get(entityId);

        Transform& transform = registry->get<Transform>(entityId);
        if (!packet->payload->Deserialize(transform))
        {
            DebugHandler::PrintError("Failed to Deserialize transform for entity(%u)", entt::to_integral(entityId));
            return false;
        }

        if (entityId == localplayerSingleton.entity)
        {
            CameraFreeLook* freelookCamera = ServiceLocator::GetCameraFreeLook();
            if (freelookCamera->IsActive())
            {
                freelookCamera->SetPosition(transform.position);
                freelookCamera->SetYaw(transform.rotation.z);
            }
        }

        registry->emplace_or_replace<TransformIsDirty>(entityId);

        return true;
    }
    bool GameHandlers::HandleDeleteEntity(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        entt::registry* registry = ServiceLocator::GetGameRegistry();
        LocalplayerSingleton& localplayerSingleton = registry->ctx<LocalplayerSingleton>();

        entt::entity entity = entt::null;
        packet->payload->Get(entity);

        auto itr = std::find(receivedEntityIDs.begin(), receivedEntityIDs.end(), entt::to_integral(entity));
        receivedEntityIDs.erase(itr);

        if (localplayerSingleton.entity == entity)
            return true;

        if (registry->valid(entity))
        {
            registry->destroy(entity);
        }
        return true;
    }
    bool GameHandlers::HandleStoreLocAck(std::shared_ptr<NetClient> netClient, std::shared_ptr<NetPacket> packet)
    {
        GameConsole* gameConsole = ServiceLocator::GetGameConsole();

        u8 status = 0;
        std::string name = "";

        if (!packet->payload->GetU8(status))
            return false;

        if (!packet->payload->GetString(name))
            return false;

        if (status == 0)
        {
            gameConsole->PrintWarning("A Location with the same name already exists (Name : %s)", name.c_str());
        }
        else
        {
            vec3 position = vec3(0.0f, 0.0f, 0.0f);
            f32 orientation = 0.0f;

            if (!packet->payload->Get(position))
                return false;

            if (!packet->payload->GetF32(orientation))
                return false;

            gameConsole->PrintSuccess("Added Location (Name : '%s', Position: (X: %f, Y: %f, Z: %f), Orientation: %f)", name.c_str(), position.x, position.y, position.z, orientation);
        }

        return true;
    }
}