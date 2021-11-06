#pragma once

#include <NovusTypes.h>

#include <vector>
#include <Renderer/DescriptorSet.h>
#include <Renderer/Descriptors/BufferDesc.h>
#include <Renderer/Descriptors/ImageDesc.h>
#include <Renderer/Descriptors/DepthImageDesc.h>
#include <Renderer/GPUVector.h>

#include "RenderResources.h"

namespace Renderer
{
	class Renderer;
	class RenderGraph;
	class CommandList;
};

struct RenderResources;

class DebugRenderer
{
public:
	DebugRenderer(Renderer::Renderer* renderer, RenderResources& resources);

	void Update(f32 deltaTime);
	
	void Add2DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);
	void Add3DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex);

	void DrawLine2D(const vec2& from, const vec2& to, uint32_t color);
	void DrawLine3D(const vec3& from, const vec3& to, uint32_t color);

	void DrawAABB3D(const vec3& center, const vec3& extents, uint32_t color);
	void DrawTriangle2D(const vec2& v0, const vec2& v1, const vec2& v2, uint32_t color);
	void DrawTriangle3D(const vec3& v0, const vec3& v1, const vec3& v2, uint32_t color);

	void DrawFrustum(const mat4x4& viewProjectionMatrix, uint32_t color);
	void DrawMatrix(const mat4x4& matrix, f32 scale);

	static vec3 UnProject(const vec3& point, const mat4x4& m);

private:

	

private:

	struct DebugVertex2D
	{
		glm::vec2 pos;
		uint32_t color;
	};

	struct DebugVertex3D
	{
		glm::vec3 pos;
		uint32_t color;
	};

	Renderer::Renderer* _renderer = nullptr;

	Renderer::GPUVector<DebugVertex2D> _debugVertices2D;
	Renderer::GPUVector<DebugVertex3D> _debugVertices3D;
	
	Renderer::DescriptorSet _draw2DDescriptorSet;
	Renderer::DescriptorSet _draw3DDescriptorSet;
};