#pragma once
#include <Utils/SafeVector.h>
#include <Utils/DebugHandler.h>
#include <Memory/BufferRangeAllocator.h>
#include "Descriptors/BufferDesc.h"
#include "Renderer.h"
#include "CommandList.h"
#include "RenderSettings.h"

namespace Renderer
{
    // This is a combined SafeVector<T> with a backing GPU Buffer and BufferRangeAllocator keeping track of the offsets of the GPU Buffer
    template <typename T>
    class GPUVector : public SafeVector<T>
    {
        struct DirtyRegion
        {
            size_t offset;
            size_t size;
        };

    public:
        void SetDirtyRegion(size_t offset, size_t size)
        {
            _dirtyRegions.WriteLock([&](std::vector<DirtyRegion>& dirtyRegions)
            {
                DirtyRegion& dirtyRegion = dirtyRegions.emplace_back();

                dirtyRegion.offset = offset;
                dirtyRegion.size = size;
            });
        }

        void SetDirtyElement(size_t elementIndex)
        {
            SetDirtyRegion(elementIndex * sizeof(T), sizeof(T));
        }
        
        void SetDirtyElements(size_t startIndex, size_t count)
        {
            SetDirtyRegion(startIndex * sizeof(T), count * sizeof(T));
        }

        // Returns true if we had to resize the buffer
        bool SyncToGPU(Renderer* renderer)
        {
            std::unique_lock lock(_mutex);

            size_t vectorByteSize = _vector.size() * sizeof(T);

            if (!_initialized)
            {
                _renderer = renderer;
                _allocator.Init(0, 0);
                _initialized = true;

                if (vectorByteSize == 0) // Not sure about this
                {
                    ResizeBuffer(renderer, 1, false);
                }
            }

            if (vectorByteSize == 0) // Not sure about this
            {
                return false;
            }

            size_t allocatedBytes = _allocator.AllocatedBytes();
            if (vectorByteSize == allocatedBytes)
            {
                // We don't need to resize the buffer, but we might have dirty regions that we need to update
                UpdateDirtyRegions(renderer);
                return false; 
            }
            size_t bufferByteSize = _allocator.Size();

            bool didResize = false;
            if (vectorByteSize > bufferByteSize)
            {
                ResizeBuffer(renderer, vectorByteSize, true); // This copies everything that was allocated in the old buffer to the new buffer
                bufferByteSize = _allocator.Size();
                didResize = true;
            }
            
            // Allocate and upload anything that has been added since last sync
            size_t bytesToAllocate = bufferByteSize - allocatedBytes;

            if (bytesToAllocate > 0)
            {
                BufferRangeFrame bufferRangeFrame;
                if (!_allocator.Allocate(bytesToAllocate, bufferRangeFrame))
                {
                    DebugHandler::PrintFatal("GPUVector : Failed to allocate GPU Vector %s", _debugName.c_str());
                }
            }

            // Upload everything between allocatedBytes and allocatedBytes+bytesToAllocate
            renderer->UploadToBuffer(_buffer, allocatedBytes, _vector.data(), allocatedBytes, bytesToAllocate);

            UpdateDirtyRegions(renderer);

            return didResize;
        }

        // Returns true if we had to resize the buffer
        bool ForceSyncToGPU(Renderer* renderer)
        {
            std::unique_lock lock(_mutex);

            size_t vectorByteSize = _vector.size() * sizeof(T);

            if (vectorByteSize == 0) // Not sure about this
                return false;

            if (!_initialized)
            {
                _renderer = renderer;
                _allocator.Init(0, 0);
                _initialized = true;
            }

            size_t allocatedBytes = _allocator.AllocatedBytes();
            size_t bufferByteSize = _allocator.Size();

            bool didResize = false;
            if (vectorByteSize > bufferByteSize)
            {
                ResizeBuffer(renderer, vectorByteSize, false);
                bufferByteSize = _allocator.Size();
                didResize = true;
            }

            // Allocate the part of the buffer that wasn't allocated before
            size_t bytesToAllocate = bufferByteSize - allocatedBytes;

            if (bytesToAllocate > 0)
            {
                BufferRangeFrame bufferRangeFrame;
                if (!_allocator.Allocate(bytesToAllocate, bufferRangeFrame))
                {
                    DebugHandler::PrintFatal("GPUVector : Failed to allocate GPU Vector %s", _debugName.c_str());
                }
            }

            // Then upload the whole buffer
            renderer->UploadToBuffer(_buffer, 0, _vector.data(), 0, vectorByteSize);

            return didResize;
        }

        void SetDebugName(const std::string& debugName)
        {
            _debugName = debugName;
        }

        void SetUsage(u8 usage)
        {
            _usage = usage;
        }

        // This shadows Clear() in SafeVector
        void Clear()
        {
            std::unique_lock lock(_mutex);
            _vector.clear();

            _allocator.Init(0, 0);

            if (_renderer != nullptr && _buffer != BufferID::Invalid())
            {
                _renderer->QueueDestroyBuffer(_buffer);
                _buffer = BufferID::Invalid();
            }

            _dirtyRegions.Clear();
            _initialized = false;
        }

        BufferID GetBuffer() { return _buffer; }

    private:
        void ResizeBuffer(Renderer* renderer, size_t newSize, bool copyOld)
        {
            BufferDesc desc;
            desc.name = _debugName;
            desc.size = newSize;
            desc.usage = _usage | BufferUsage::TRANSFER_SOURCE | BufferUsage::TRANSFER_DESTINATION;

            BufferID newBuffer = renderer->CreateBuffer(desc);

            if (_buffer != BufferID::Invalid())
            {
                if (copyOld)
                {
                    size_t oldSize = _allocator.AllocatedBytes();
                    if (oldSize > 0)
                    {
                        renderer->CopyBuffer(newBuffer, 0, _buffer, 0, oldSize);
                    }
                }
                renderer->QueueDestroyBuffer(_buffer);
            }

            _allocator.Grow(newSize);
            _buffer = newBuffer;
        }

        void UpdateDirtyRegions(Renderer* renderer)
        {
            _dirtyRegions.WriteLock([&](std::vector<DirtyRegion>& dirtyRegions)
            {
                if (dirtyRegions.size() == 0)
                    return;

                // Sort dirtyRegions by offset
                std::sort(dirtyRegions.begin(), dirtyRegions.end(), [](const DirtyRegion& a, const DirtyRegion& b)
                {
                    return a.offset < b.offset;
                });

                // Try to merge dirty regions
                for (i32 i = static_cast<i32>(dirtyRegions.size()) - 2; i >= 0; i--)
                {
                    DirtyRegion& curRegion = dirtyRegions[i]; // The currently iterated region
                    DirtyRegion& nextRegion = dirtyRegions[i + 1]; // Current+1, the one we're trying to remove

                    // TODO: See if there is any overlap between regions

                    // Merge regions that are next to eachother
                    size_t curRegionEnd = curRegion.offset + curRegion.size;
                    if (nextRegion.offset == curRegionEnd)
                    {
                        curRegion.size += nextRegion.size;
                        dirtyRegions.erase(dirtyRegions.begin() + i + 1);
                    }
                }
                
                // Upload for all remaining dirtyRegions
                for (const DirtyRegion& dirtyRegion : dirtyRegions)
                {
                    renderer->UploadToBuffer(_buffer, dirtyRegion.offset, _vector.data(), dirtyRegion.offset, dirtyRegion.size);
                }
                dirtyRegions.clear();
            });
        }

        bool _initialized = false;
        Renderer* _renderer = nullptr;
        BufferID _buffer;
        BufferRangeAllocator _allocator;

        std::string _debugName = "";
        u8 _usage = 0;

        SafeVector<DirtyRegion> _dirtyRegions;
    };
}