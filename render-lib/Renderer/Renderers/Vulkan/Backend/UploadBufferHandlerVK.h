#pragma once
#include <NovusTypes.h>
#include <Memory/StackAllocator.h>

#include "../../../Descriptors/UploadBuffer.h"
#include "../../../Descriptors/TextureDesc.h"
#include "../../../Descriptors/SemaphoreDesc.h"

struct VkCommandBuffer_T;
typedef VkCommandBuffer_T* VkCommandBuffer;

namespace Renderer
{
    class RendererVK;

    namespace Backend
    {
        class RenderDeviceVK;
        class BufferHandlerVK;
        class TextureHandlerVK;
        class SemaphoreHandlerVK;
        class CommandListHandlerVK;

        struct StagingBuffer;

        struct IUploadBufferHandlerVKData {};

        class UploadBufferHandlerVK
        {
        public:
            void Init(RenderDeviceVK* device, BufferHandlerVK* bufferHandler, TextureHandlerVK* textureHandler, SemaphoreHandlerVK* semaphoreHandler, CommandListHandlerVK* commandListHandler);
            void ExecuteUploadTasks();

            [[nodiscard]] std::shared_ptr<UploadBuffer> CreateUploadBuffer(BufferID targetBuffer, size_t targetOffset, size_t size);
            [[nodiscard]] std::shared_ptr<UploadBuffer> CreateUploadBuffer(TextureID targetTexture, size_t targetOffset, size_t size);

            SemaphoreID GetUploadFinishedSemaphore();
            bool ShouldWaitForUpload();
            void SetHasWaitedForUpload();
        private:
            size_t Allocate(size_t size, StagingBufferID& stagingBufferID, void*& mappedMemory);
            void ExecuteStagingBuffer(VkCommandBuffer commandBuffer, StagingBuffer& stagingBuffer);
            void ExecuteStagingBuffer(StagingBuffer& stagingBuffer);
            void WaitForStagingBuffer(StagingBuffer& stagingBuffer);

            void RunSubmitThread();

        private:
            RenderDeviceVK* _device;
            BufferHandlerVK* _bufferHandler;
            TextureHandlerVK* _textureHandler;
            SemaphoreHandlerVK* _semaphoreHandler;
            CommandListHandlerVK* _commandListHandler;

            IUploadBufferHandlerVKData* _data;
        };
    }
}