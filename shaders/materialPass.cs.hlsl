permutation DEBUG_ID = [0, 1, 2, 3];

#include "globalData.inc.hlsl"
#include "visibilityBuffer.inc.hlsl"
#include "terrain.inc.hlsl"
#include "mapObject.inc.hlsl"
#include "cModel.inc.hlsl"

[[vk::binding(0, PER_PASS)]] SamplerState _sampler;
[[vk::binding(3, PER_PASS)]] Texture2D<float4> _transparency;
[[vk::binding(4, PER_PASS)]] Texture2D<float> _transparencyWeights;

float4 ShadeTerrain(const uint2 pixelPos, const VisibilityBuffer vBuffer)
{
	CellInstance cellInstance = _cellInstances[vBuffer.drawID];
	uint globalCellID = cellInstance.globalCellID;

	// Terrain code
	uint globalVertexOffset = globalCellID * NUM_VERTICES_PER_CELL;
	uint3 localVertexIDs = GetLocalTerrainVertexIDs(vBuffer.triangleID);

	const uint cellID = cellInstance.packedChunkCellID & 0xFFFF;
	const uint chunkID = cellInstance.packedChunkCellID >> 16;

	// Get vertices
	TerrainVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadTerrainVertex(chunkID, cellID, globalVertexOffset, localVertexIDs[i]);
	}

	// Interpolate vertex attributes
	FullBary pixelUV = CalcFullBary(vBuffer.barycentrics, vertices[0].uv, vertices[1].uv, vertices[2].uv); // [0..8] This is correct for terrain color textures

	float3 pixelColor = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].color, vertices[1].color, vertices[2].color);
	float3 pixelNormal = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal);
	
	float3 pixelAlphaUV = float3(saturate(pixelUV.value / 8.0f), float(cellID)); // However the alpha needs to be between 0 and 1, and load from the correct layer

	// Get CellData and ChunkData
	const CellData cellData = LoadCellData(globalCellID);

	const uint globalChunkID = globalCellID / NUM_CELLS_PER_CHUNK;
	const ChunkData chunkData = _chunkData[globalChunkID];

	// We have 4 uints per chunk for our diffuseIDs, this gives us a size and alignment of 16 bytes which is exactly what GPUs want
	// However, we need a fifth uint for alphaID, so we decided to pack it into the LAST diffuseID, which gets split into two uint16s
	// This is what it looks like
	// [1111] diffuseIDs.x
	// [2222] diffuseIDs.y
	// [3333] diffuseIDs.z
	// [AA44] diffuseIDs.w Alpha is read from the most significant bits, the fourth diffuseID read from the least 
	uint diffuse0ID = cellData.diffuseIDs.x;
	uint diffuse1ID = cellData.diffuseIDs.y;
	uint diffuse2ID = cellData.diffuseIDs.z;
	uint diffuse3ID = cellData.diffuseIDs.w;
	uint alphaID = chunkData.alphaID;

	float3 alpha = _terrainAlphaTextures[NonUniformResourceIndex(alphaID)].SampleGrad(_alphaSampler, pixelAlphaUV, pixelUV.ddx, pixelUV.ddy).rgb;
	float minusAlphaBlendSum = (1.0 - clamp(alpha.x + alpha.y + alpha.z, 0.0, 1.0));
	float4 weightsVector = float4(minusAlphaBlendSum, alpha);

	float4 color = float4(0, 0, 0, 1);

	float3 diffuse0 = _terrainColorTextures[NonUniformResourceIndex(diffuse0ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.x;
	color.rgb += diffuse0;

	float3 diffuse1 = _terrainColorTextures[NonUniformResourceIndex(diffuse1ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.y;
	color.rgb += diffuse1;

	float3 diffuse2 = _terrainColorTextures[NonUniformResourceIndex(diffuse2ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.z;
	color.rgb += diffuse2;

	float3 diffuse3 = _terrainColorTextures[NonUniformResourceIndex(diffuse3ID)].SampleGrad(_sampler, pixelUV.value, pixelUV.ddx, pixelUV.ddy).xyz * weightsVector.w;
	color.rgb += diffuse3;

	// Apply lighting
	float3 normal = normalize(pixelNormal);
	float ambientOcclusion = _ambientOcclusion.Load(float3(pixelPos, 0)).x;
	color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), normal, ambientOcclusion, true);

	return saturate(color);
}

float4 ShadeMapObject(const uint2 pixelPos, const VisibilityBuffer vBuffer)
{
	InstanceLookupData lookupData = LoadInstanceLookupData(vBuffer.drawID);
	InstanceData instanceData = _mapObjectInstanceData[lookupData.instanceID];

	// Get Vertices
	Draw draw = _mapObjectDraws[vBuffer.drawID];
    uint3 vertexIDs = GetVertexIDs(vBuffer.triangleID, draw, _mapObjectIndices);

	MapObjectVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadMapObjectVertex(vertexIDs[i], lookupData);

		// Convert normals to world normals
		vertices[i].normal = mul(vertices[i].normal, (float3x3)instanceData.instanceMatrix);
	}

	// Interpolate vertex attributes
	FullBary pixelUV0 = CalcFullBary(vBuffer.barycentrics, vertices[0].uv.xy, vertices[1].uv.xy, vertices[2].uv.xy);
	FullBary pixelUV1 = CalcFullBary(vBuffer.barycentrics, vertices[0].uv.zw, vertices[1].uv.zw, vertices[2].uv.zw);
		 
	float3 pixelNormal = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal);
	float4 pixelColor0 = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].color0, vertices[1].color0, vertices[2].color0);
	float4 pixelColor1 = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].color1, vertices[1].color1, vertices[2].color1);

	// Shade
	MapObjectMaterialParam materialParam = LoadMapObjectMaterialParam(lookupData.materialParamID);
	MapObjectMaterial material = LoadMapObjectMaterial(materialParam.materialID);

	uint textureID0 = material.textureIDs[0]; // Prevents invalid patching of shader when running GPU validation layers, maybe remove in future
	float4 tex0 = _mapObjectTextures[NonUniformResourceIndex(textureID0)].SampleGrad(_sampler, pixelUV0.value, pixelUV0.ddx, pixelUV0.ddy);

	uint textureID1 = material.textureIDs[1]; // Prevents invalid patching of shader when running GPU validation layers, maybe remove in future
	float4 tex1 = _mapObjectTextures[NonUniformResourceIndex(textureID1)].SampleGrad(_sampler, pixelUV1.value, pixelUV1.ddx, pixelUV1.ddy);

	float3 normal = normalize(pixelNormal);
	bool isLit = materialParam.exteriorLit == 1;
	float ambientOcclusion = _ambientOcclusion.Load(float3(pixelPos, 0)).x;

	float4 color = float4(0, 0, 0, 1);
	if (material.materialType == 0) // Diffuse
	{
		float3 matDiffuse = tex0.rgb;
		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit), pixelColor0.a);
	}
	else if (material.materialType == 1) // Specular
	{
		float3 matDiffuse = tex0.rgb;
		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit), pixelColor0.a);
	}
	else if (material.materialType == 2) // Metal
	{
		float3 matDiffuse = tex0.rgb;
		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit), pixelColor0.a);
	}
	else if (material.materialType == 3) // Environment
	{
		float3 matDiffuse = tex0.rgb;
		float3 env = tex1.rgb * tex0.a;
		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit) + env, pixelColor0.a);
	}
	else if (material.materialType == 4) // Opaque
	{
		float3 matDiffuse = tex0.rgb;
		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit), pixelColor0.a);
	}
	else if (material.materialType == 5) // Environment metal
	{
		float3 matDiffuse = tex0.rgb;
		float3 env = (tex0.rgb * tex0.a) * tex1.rgb;
		color = float4(Lighting(tex0.rgb, pixelColor0.rgb, normal, ambientOcclusion, isLit) + env, pixelColor0.a);
	}
	else if (material.materialType == 6) // Two Layer Diffuse
	{
		float3 layer0 = tex0.rgb;
		float3 layer1 = lerp(layer0, tex1.rgb, tex1.a);
		float3 matDiffuse = (pixelColor0.rgb * 2.0) * lerp(layer1, layer0, pixelColor1.a);

		color = float4(Lighting(matDiffuse, pixelColor0.rgb, normal, ambientOcclusion, isLit), 1.0f);
	}

	return color;
}

float4 ShadeCModel(const uint2 pixelPos, const VisibilityBuffer vBuffer)
{
	CModelDrawCallData drawCallData = LoadCModelDrawCallData(vBuffer.drawID);
	CModelInstanceData instanceData = _cModelInstances[drawCallData.instanceID];

	// Get the VertexIDs of the triangle we're in
	Draw draw = _cModelDraws[vBuffer.drawID];
	uint3 vertexIDs = GetVertexIDs(vBuffer.triangleID, draw, _cModelIndices);

	// Get Vertices
	CModelVertex vertices[3];

	[unroll]
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = LoadCModelVertex(vertexIDs[i]);

		// Animate the vertex normal if we need to
		if (instanceData.boneDeformOffset != 4294967295)
		{
			// Calculate bone transform matrix
			float4x4 boneTransformMatrix = CalcBoneTransformMatrix(instanceData, vertices[i]);
			vertices[i].normal = mul(vertices[i].normal, (float3x3)boneTransformMatrix);
		}

		// Convert normals to world normals
		vertices[i].normal = mul(vertices[i].normal, (float3x3)instanceData.instanceMatrix);
	}

	// Interpolate vertex attributes
	FullBary pixelUV0 = CalcFullBary(vBuffer.barycentrics, vertices[0].uv01.xy, vertices[1].uv01.xy, vertices[2].uv01.xy);
	FullBary pixelUV1 = CalcFullBary(vBuffer.barycentrics, vertices[0].uv01.zw, vertices[1].uv01.zw, vertices[2].uv01.zw);

	float3 pixelNormal = InterpolateVertexAttribute(vBuffer.barycentrics, vertices[0].normal, vertices[1].normal, vertices[2].normal);

	// Shade
	float4 color = float4(0, 0, 0, 0);
	float3 specular = float3(0, 0, 0);
	bool isUnlit = false;

	for (uint textureUnitIndex = drawCallData.textureUnitOffset; textureUnitIndex < drawCallData.textureUnitOffset + drawCallData.numTextureUnits; textureUnitIndex++)
	{
		CModelTextureUnit textureUnit = _cModelTextureUnits[textureUnitIndex];

		uint isProjectedTexture = textureUnit.data1 & 0x1;
		uint materialFlags = (textureUnit.data1 >> 1) & 0x3FF;
		uint blendingMode = (textureUnit.data1 >> 11) & 0x7;

		uint materialType = (textureUnit.data1 >> 16) & 0xFFFF;
		uint vertexShaderId = materialType & 0xFF;
		uint pixelShaderId = materialType >> 8;

		if (materialType == 0x8000)
			continue;

		float4 texture0 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[0])].SampleGrad(_sampler, pixelUV0.value, pixelUV0.ddx, pixelUV0.ddy);
		float4 texture1 = float4(0, 0, 0, 0);

		if (vertexShaderId > 2)
		{
			// ENV uses generated UVCoords based on camera pos + geometry normal in frame space
			texture1 = _cModelTextures[NonUniformResourceIndex(textureUnit.textureIDs[1])].SampleGrad(_sampler, pixelUV1.value, pixelUV1.ddx, pixelUV1.ddy);
		}

		isUnlit |= (materialFlags & 0x1);

		float4 shadedColor = ShadeCModel(pixelShaderId, texture0, texture1, specular);
		color = BlendCModel(blendingMode, color, shadedColor);
	}

	// Apply lighting
	float ambientOcclusion = _ambientOcclusion.Load(float3(pixelPos, 0)).x;
	color.rgb = Lighting(color.rgb, float3(0.0f, 0.0f, 0.0f), pixelNormal, ambientOcclusion, !isUnlit) + specular;

	return saturate(color);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixelPos = dispatchThreadId.xy;

	float2 dimensions;
	_resolvedColor.GetDimensions(dimensions.x, dimensions.y);

	if (any(pixelPos > dimensions))
	{
		return;
	}

	uint4 vBufferData = LoadVisibilityBuffer(pixelPos);
	const VisibilityBuffer vBuffer = UnpackVisibilityBuffer(vBufferData);

#if DEBUG_ID == 1 // TypeID debug output
	float3 debugColor = IDToColor3(vBuffer.typeID);
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#elif DEBUG_ID == 2 // ObjectID debug output
	float3 debugColor = IDToColor3(GetObjectID(vBuffer.typeID, vBuffer.drawID));
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#elif DEBUG_ID == 3 // TriangleID
	float3 debugColor = IDToColor3(vBuffer.triangleID);
	_resolvedColor[pixelPos] = float4(debugColor, 1);
	return;
#endif

	float4 color = float4(0,0,0,1);
	if (vBuffer.typeID == ObjectType::Skybox)
	{
		color = PackedUnormsToFloat4(vBufferData.y); // Skybox is a unique case that packs the resulting color in the Y component of the visibility buffer
	}
	else if (vBuffer.typeID == ObjectType::Terrain)
	{
		color = ShadeTerrain(pixelPos, vBuffer);
	}
	else if (vBuffer.typeID == ObjectType::MapObject)
	{
		color = ShadeMapObject(pixelPos, vBuffer);
	}
	else if (vBuffer.typeID == ObjectType::CModelOpaque) // Transparent cmodels are not rendered using visibility buffers
	{
		color = ShadeCModel(pixelPos, vBuffer);
	}
	else
	{
		color.rg = vBuffer.barycentrics.bary;
	}

	// Composite Transparency
	float4 transparency = _transparency.Load(uint3(pixelPos, 0));
	float transparencyWeight = _transparencyWeights.Load(uint3(pixelPos, 0));

	float3 transparencyColor = transparency.rgb / max(transparency.a, 1e-5);

	// Src: ONE_MINUS_SRC_ALPHA, Dst: SRC_ALPHA
	color.rgb = (transparencyColor.rgb * (1.0f - transparencyWeight)) + (color.rgb /** transparencyWeight*/);

	_resolvedColor[pixelPos] = color;
}