#include "DebugRenderer.h"

#include <Renderer/Renderer.h>
#include <Renderer/RenderGraph.h>
#include <Renderer/CommandList.h>

#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>

#include <glm/gtx/transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

DebugRenderer::DebugRenderer(Renderer::Renderer* renderer, RenderResources& resources)
{
	_renderer = renderer;

	_debugVertices2D.SetDebugName("DebugVertices2D");
	_debugVertices2D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
	_debugVertices2D.SyncToGPU(_renderer);
	_draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());

	_debugVertices3D.SetDebugName("DebugVertices3D");
	_debugVertices3D.SetUsage(Renderer::BufferUsage::TRANSFER_DESTINATION | Renderer::BufferUsage::STORAGE_BUFFER);
	_debugVertices3D.SyncToGPU(_renderer);
	_draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());
}

void DebugRenderer::Update(f32 deltaTime)
{
	// Sync to GPU
	if (_debugVertices2D.SyncToGPU(_renderer))
	{
		_draw2DDescriptorSet.Bind("_vertices", _debugVertices2D.GetBuffer());
	}
	if (_debugVertices3D.SyncToGPU(_renderer))
	{
		_draw3DDescriptorSet.Bind("_vertices", _debugVertices3D.GetBuffer());
	}
}

void DebugRenderer::Add2DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Debug2DPassData
	{
		Renderer::RenderPassMutableResource color;
	};
	renderGraph->AddPass<Debug2DPassData>("DebugRender2D",
		[=](Debug2DPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			data.color = builder.Write(resources.resolvedColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[=](Debug2DPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender2D);

			Renderer::GraphicsPipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Rasterizer state
			pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;

			// Render targets.
			pipelineDesc.renderTargets[0] = data.color;

			// Shader
			Renderer::VertexShaderDesc vertexShaderDesc;
			vertexShaderDesc.path = "debug2D.vs.hlsl";

			Renderer::PixelShaderDesc pixelShaderDesc;
			pixelShaderDesc.path = "debug2D.ps.hlsl";

			pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
			pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

			pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;

			// Set pipeline
			Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
			commandList.BeginPipeline(pipeline);

			commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
			commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw2DDescriptorSet, frameIndex);

			// Draw
			commandList.Draw(static_cast<u32>(_debugVertices2D.Size()), 1, 0, 0);

			commandList.EndPipeline(pipeline);
			_debugVertices2D.Clear(false);
		});
}

void DebugRenderer::Add3DPass(Renderer::RenderGraph* renderGraph, RenderResources& resources, u8 frameIndex)
{
	struct Debug3DPassData
	{
		Renderer::RenderPassMutableResource color;
		Renderer::RenderPassMutableResource depth;
	};
	renderGraph->AddPass<Debug3DPassData>("DebugRender3D",
		[=](Debug3DPassData& data, Renderer::RenderGraphBuilder& builder) // Setup
		{
			data.color = builder.Write(resources.resolvedColor, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);
			data.depth = builder.Write(resources.depth, Renderer::RenderGraphBuilder::WriteMode::RENDERTARGET, Renderer::RenderGraphBuilder::LoadMode::LOAD);

			return true;// Return true from setup to enable this pass, return false to disable it
		},
		[=](Debug3DPassData& data, Renderer::RenderGraphResources& graphResources, Renderer::CommandList& commandList) // Execute
		{
			GPU_SCOPED_PROFILER_ZONE(commandList, DebugRender3D);

			Renderer::GraphicsPipelineDesc pipelineDesc;
			graphResources.InitializePipelineDesc(pipelineDesc);

			// Shader
			Renderer::VertexShaderDesc vertexShaderDesc;
			vertexShaderDesc.path = "debug3D.vs.hlsl";

			Renderer::PixelShaderDesc pixelShaderDesc;
			pixelShaderDesc.path = "debug3D.ps.hlsl";

			pipelineDesc.states.vertexShader = _renderer->LoadShader(vertexShaderDesc);
			pipelineDesc.states.pixelShader = _renderer->LoadShader(pixelShaderDesc);

			pipelineDesc.states.primitiveTopology = Renderer::PrimitiveTopology::Lines;

			// Depth state
			pipelineDesc.states.depthStencilState.depthEnable = true;
			pipelineDesc.states.depthStencilState.depthWriteEnable = false;
			pipelineDesc.states.depthStencilState.depthFunc = Renderer::ComparisonFunc::GREATER;

			// Rasterizer state
			pipelineDesc.states.rasterizerState.cullMode = Renderer::CullMode::BACK;
			pipelineDesc.states.rasterizerState.frontFaceMode = Renderer::FrontFaceState::COUNTERCLOCKWISE;

			pipelineDesc.renderTargets[0] = data.color;

			pipelineDesc.depthStencil = data.depth;

			// Set pipeline
			Renderer::GraphicsPipelineID pipeline = _renderer->CreatePipeline(pipelineDesc); // This will compile the pipeline and return the ID, or just return ID of cached pipeline
			commandList.BeginPipeline(pipeline);

			commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::GLOBAL, &resources.globalDescriptorSet, frameIndex);
			commandList.BindDescriptorSet(Renderer::DescriptorSetSlot::PER_PASS, &_draw3DDescriptorSet, frameIndex);

			// Draw
			commandList.Draw(static_cast<u32>(_debugVertices3D.Size()), 1, 0, 0);

			commandList.EndPipeline(pipeline);
			_debugVertices3D.Clear(false);
		});
}

void DebugRenderer::DrawLine2D(const glm::vec2& from, const glm::vec2& to, uint32_t color)
{
	_debugVertices2D.PushBack({ from, color });
	_debugVertices2D.PushBack({ to, color });
}

void DebugRenderer::DrawLine3D(const glm::vec3& from, const glm::vec3& to, uint32_t color)
{
	_debugVertices3D.PushBack({ from, color });
	_debugVertices3D.PushBack({ to, color });
}

void DebugRenderer::DrawAABB3D(const vec3& center, const vec3& extents, uint32_t color)
{
	vec3 v0 = center - extents;
	vec3 v1 = center + extents;

	// Bottom
	_debugVertices3D.PushBack({ { v0.x, v0.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v0.y, v0.z }, color });

	// Top
	_debugVertices3D.PushBack({ { v0.x, v1.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v1.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v1.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v1.y, v0.z }, color });

	// Vertical edges
	_debugVertices3D.PushBack({ { v0.x, v0.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v1.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v0.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v0.x, v1.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v0.y, v1.z }, color });
	_debugVertices3D.PushBack({ { v1.x, v1.y, v1.z }, color });
}

void DebugRenderer::DrawTriangle2D(const glm::vec2& v0, const glm::vec2& v1, const glm::vec2& v2, uint32_t color)
{
	DrawLine2D(v0, v1, color);
	DrawLine2D(v1, v2, color);
	DrawLine2D(v2, v0, color);
}

void DebugRenderer::DrawTriangle3D(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, uint32_t color)
{
	DrawLine3D(v0, v1, color);
	DrawLine3D(v1, v2, color);
	DrawLine3D(v2, v0, color);
}

vec3 DebugRenderer::UnProject(const vec3& point, const mat4x4& m)
{
	vec4 obj = m * vec4(point, 1.0f);
	obj /= obj.w;
	return vec3(obj);
}

void DebugRenderer::DrawFrustum(const mat4x4& viewProjectionMatrix, uint32_t color)
{
	const mat4x4 m = glm::inverse(viewProjectionMatrix);

	vec4 viewport(0.0f, 0.0f, 640.0f, 360.0f);

	const vec3 near0 = UnProject(vec3(-1.0f, -1.0f, 0.0f), m);
	const vec3 near1 = UnProject(vec3(+1.0f, -1.0f, 0.0f), m);
	const vec3 near2 = UnProject(vec3(+1.0f, +1.0f, 0.0f), m);
	const vec3 near3 = UnProject(vec3(-1.0f, +1.0f, 0.0f), m);

	const vec3 far0 = UnProject(vec3(-1.0f, -1.0f, 1.0f), m);
	const vec3 far1 = UnProject(vec3(+1.0f, -1.0f, 1.0f), m);
	const vec3 far2 = UnProject(vec3(+1.0f, +1.0f, 1.0f), m);
	const vec3 far3 = UnProject(vec3(-1.0f, +1.0f, 1.0f), m);

	// Near plane
	DrawLine3D(near0, near1, color);
	DrawLine3D(near1, near2, color);
	DrawLine3D(near2, near3, color);
	DrawLine3D(near3, near0, color);

	// Far plane
	DrawLine3D(far0, far1, color);
	DrawLine3D(far1, far2, color);
	DrawLine3D(far2, far3, color);
	DrawLine3D(far3, far0, color);

	// Edges
	DrawLine3D(near0, far0, color);
	DrawLine3D(near1, far1, color);
	DrawLine3D(near2, far2, color);
	DrawLine3D(near3, far3, color);
}

void DebugRenderer::DrawMatrix(const mat4x4& matrix, f32 scale)
{
	const vec3 origin = vec3(matrix[3].x, matrix[3].y, matrix[3].z);

	DrawLine3D(origin, origin + (vec3(matrix[0].x, matrix[0].y, matrix[0].z) * scale), 0xff0000ff);
	DrawLine3D(origin, origin + (vec3(matrix[1].x, matrix[1].y, matrix[1].z) * scale), 0xff00ff00);
	DrawLine3D(origin, origin + (vec3(matrix[2].x, matrix[2].y, matrix[2].z) * scale), 0xffff0000);
}
