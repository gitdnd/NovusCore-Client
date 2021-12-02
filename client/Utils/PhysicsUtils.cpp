#include "PhysicsUtils.h"
#include "MapUtils.h"

#include "../Rendering/ClientRenderer.h"
#include "../Rendering/DebugRenderer.h"
#include "../Rendering/CModelRenderer.h"

#include <Gameplay/ECS/Components/Movement.h>
#include "../ECS/Components/Rendering/CModelInfo.h"
#include "../ECS/Components/Singletons/TimeSingleton.h"

#include <Math/Geometry.h>

namespace PhysicsUtils
{
#pragma warning( push )
#pragma warning( disable : 4723 )
    void Project(const vec3& vertex, const vec3& axis, vec2& minMax)
    {
        f32 val = glm::dot(axis, vertex);
        if (val < minMax.x) minMax.x = val;
        if (val > minMax.y) minMax.y = val;
    }
    void ProjectTriangle(const Geometry::Triangle& triangle, const vec3& axis, vec2& minMax)
    {
        minMax.x = 100000.0f;
        minMax.y = -100000.0f;

        Project(triangle.vert1, axis, minMax);
        Project(triangle.vert2, axis, minMax);
        Project(triangle.vert3, axis, minMax);
    }
    void ProjectBox(const Geometry::AABoundingBox& box, const vec3& axis, vec2& minMax)
    {
        minMax.x = 100000.0f;
        minMax.y = -100000.0f;

        vec3 boxMin = box.center - box.extents;
        vec3 boxMax = box.center + box.extents;

        Project(boxMin, axis, minMax);
        Project({ boxMin.x, boxMin.y, boxMax.z }, axis, minMax);
        Project({ boxMin.x, boxMax.y, boxMin.z }, axis, minMax);
        Project({ boxMin.x, boxMax.y, boxMax.z }, axis, minMax);

        Project(boxMax, axis, minMax);
        Project({ boxMax.x, boxMin.y, boxMin.z }, axis, minMax);
        Project({ boxMax.x, boxMax.y, boxMin.z }, axis, minMax);
        Project({ boxMax.x, boxMin.y, boxMax.z }, axis, minMax);
    }

    bool TestOverlap(const f32& boxExt, bool& validMTD, const f32& triMin, const f32& triMax, f32& d0, f32& d1)
    {
        d0 = -boxExt - triMax;
        d1 = boxExt - triMin;
        const bool intersects = (d0 <= 0.0f && d1 >= 0.0f);
        validMTD &= intersects;

        return intersects;
    }
    i32 TestAxis(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, const vec3& axis, bool& validMTD, f32& tFirst, f32& tLast)
    {
        const f32 d0t = glm::dot(triangle.vert1, axis);
        const f32 d1t = glm::dot(triangle.vert2, axis);
        const f32 d2t = glm::dot(triangle.vert3, axis);

        f32 triMin = glm::min(d0t, d1t);
        f32 triMax = glm::max(d0t, d1t);

        triMin = glm::min(triMin, d2t);
        triMax = glm::max(triMax, d2t);

        const f32 boxExt = glm::abs(axis.x) * boxScale.x + glm::abs(axis.y) * boxScale.y + glm::abs(axis.z) * boxScale.z;

        f32 d0 = 0;
        f32 d1 = 0;
        bool intersected = TestOverlap(boxExt, validMTD, triMin, triMax, d0, d1);

        const f32 v = glm::dot(dir, axis);
        if (glm::abs(v) < 1.0E-6f)
            return intersected;

        const f32 oneOverV = -1.0f / v;
        const f32 t0_ = d0 * oneOverV;
        const f32 t1_ = d1 * oneOverV;

        f32 t0 = glm::min(t0_, t1_);
        f32 t1 = glm::max(t0_, t1_);

        if (t0 > tLast || t1 < tFirst)
            return false;

        tLast = glm::min(t1, tLast);
        tFirst = glm::max(t0, tFirst);

        return true;
    }
    i32 TestAxisXYZ(const i32 index, const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, const f32& oneOverDir, bool& validMTD, f32& tFirst, f32& tLast)
    {
        const f32 d0t = triangle.vert1[index];
        const f32 d1t = triangle.vert2[index];
        const f32 d2t = triangle.vert3[index];

        f32 triMin = glm::min(d0t, d1t);
        f32 triMax = glm::max(d0t, d1t);

        triMin = glm::min(triMin, d2t);
        triMax = glm::max(triMax, d2t);

        const f32 boxExt = boxScale[index];

        f32 d0 = 0;
        f32 d1 = 0;
        bool intersected = TestOverlap(boxExt, validMTD, triMin, triMax, d0, d1);

        const f32 v = dir[index];
        if (glm::abs(v) < 1.0E-6f)
            return intersected;

        const f32 oneOverV = -oneOverDir;
        const f32 t0_ = d0 * oneOverV;
        const f32 t1_ = d1 * oneOverV;

        f32 t0 = glm::min(t0_, t1_);
        f32 t1 = glm::max(t0_, t1_);

        if (t0 > tLast || t1 < tFirst)
            return false;

        tLast = glm::min(t1, tLast);
        tFirst = glm::max(t0, tFirst);

        return true;
    }
    bool TestSeperationAxes(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& normal, const vec3& dir, const vec3& oneOverDir, f32 tmax, f32& t)
    {
        bool validMTD = true;

        f32 tFirst = -std::numeric_limits<f32>().max();
        f32 tLast = std::numeric_limits<f32>().max();

        // Test Triangle Normal
        if (!TestAxis(boxScale, triangle, dir, normal, validMTD, tFirst, tLast))
            return false;

        // Test Box Normals
        if (!TestAxisXYZ(0, boxScale, triangle, dir, oneOverDir.x, validMTD, tFirst, tLast))
            return false;

        if (!TestAxisXYZ(1, boxScale, triangle, dir, oneOverDir.y, validMTD, tFirst, tLast))
            return false;

        if (!TestAxisXYZ(2, boxScale, triangle, dir, oneOverDir.z, validMTD, tFirst, tLast))
            return false;

        for (u32 i = 0; i < 3; i++)
        {
            i32 j = i + 1;
            if (i == 2)
                j = 0;

            const vec3& triangleEdge = triangle.GetVert(j) - triangle.GetVert(i);

            {
                // Cross100
                const vec3& sep = vec3(0.0f, -triangleEdge.z, triangleEdge.y);
                if ((glm::dot(sep, sep) >= 1.0E-6f) && !TestAxis(boxScale, triangle, dir, sep, validMTD, tFirst, tLast))
                    return false;
            }

            {
                // Cross010
                const vec3& sep = vec3(triangleEdge.z, 0.0f, -triangleEdge.x);
                if ((glm::dot(sep, sep) >= 1.0E-6f) && !TestAxis(boxScale, triangle, dir, sep, validMTD, tFirst, tLast))
                    return false;
            }

            {
                // Cross001
                const vec3& sep = vec3(-triangleEdge.y, triangleEdge.x, 0.0f);
                if ((glm::dot(sep, sep) >= 1.0E-6f) && !TestAxis(boxScale, triangle, dir, sep, validMTD, tFirst, tLast))
                    return false;
            }
        }

        if (tFirst > tmax || tLast < 0.0f)
            return false;

        if (tFirst <= 0.0f)
        {
            if (!validMTD)
                return false;

            t = 0.0f;
        }
        else
        {
            t = tFirst;
        }

        return true;
    }

    bool Intersect_AABB_TRIANGLE(const Geometry::AABoundingBox& box, const Geometry::Triangle& triangle)
    {
        vec2 triangleMinMax;
        vec2 boxMinMax;

        vec3 boxMin = box.center - box.extents;
        vec3 boxMax = box.center + box.extents;

        // Test the box normals (x, y and z)
        constexpr vec3 boxNormals[3] = { vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1) };

        for (i32 i = 0; i < 3; i++)
        {
            const vec3& n = boxNormals[i];
            ProjectTriangle(triangle, n, triangleMinMax);

            // If true, there is no intersection possible
            if (triangleMinMax.y < boxMin[i] || triangleMinMax.x > boxMax[i])
                return false;
        }

        // Test the triangle normal
        vec3 triangleNormal = triangle.GetNormal();
        f32 triangleOffset = glm::dot(triangleNormal, triangle.vert1);
        ProjectBox(box, triangleNormal, boxMinMax);

        // If true, there is no intersection possible
        if (boxMinMax.y < triangleOffset || boxMinMax.x > triangleOffset)
            return false;

        // Test the nine edge cross-products
        vec3 triangleEdges[3] = { (triangle.vert1 - triangle.vert2), (triangle.vert2 - triangle.vert3), (triangle.vert3 - triangle.vert1) };

        for (i32 i = 0; i < 3; i++)
        {
            for (i32 j = 0; j < 3; j++)
            {
                // The box normals are the same as it's edge tangents
                vec3 axis = glm::cross(triangleEdges[i], boxNormals[j]);

                ProjectBox(box, axis, boxMinMax);
                ProjectTriangle(triangle, axis, triangleMinMax);

                // If true, there is no intersection possible
                if (boxMinMax.y < triangleMinMax.x || boxMinMax.x > triangleMinMax.y)
                    return false;
            }
        }

        return true;
    }
    bool Intersect_AABB_TRIANGLE_SWEEP(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, f32 maxDist, f32& outDistToCollision, bool backFaceCulling)
    {
        // This function assumes the triangle is "translated" as such that its position is relative to the box's center meaning the origin(0,0) is the box's center
        // This function && (TestSeperationAxes, TestAxis and TestAxisXYZ) all come from https://github.com/NVIDIAGameWorks/PhysX/blob/4.1/physx/source/geomutils/src/sweep/GuSweepBoxTriangle_SAT.h
        const vec3& oneOverDir = 1.0f / dir;
        vec3 triangleNormal = triangle.GetCollisionNormal();

        if (backFaceCulling && glm::dot(triangleNormal, dir) <= 0.0f)
            return 0;

        return TestSeperationAxes(boxScale, triangle, triangleNormal, dir, oneOverDir, maxDist, outDistToCollision);
    }
    
    bool Intersect_AABB_TERRAIN(const vec3& position, const Geometry::AABoundingBox& box, Geometry::Triangle& triangle, f32& height)
    {
        vec3 offsets[5] =
        {
            {0, 0, 0},
            {-box.extents.x, 0, -box.extents.z},
            {box.extents.x, 0, -box.extents.z},
            {-box.extents.x, 0, box.extents.z},
            {box.extents.x, 0, box.extents.z}
        };

        // TODO: Look into if we want to optimize this, the reason we currently
        //       always get the Verticies from pos is because we don't manually
        //       check for chunk/cell borders, but we could speed this part up
        //       by doing that manually instead of calling GetVerticiesFromWorldPosition
        //       5 times.
        for (i32 i = 0; i < 5; i++)
        {
            vec3 pos = position + offsets[i];

            if (Terrain::MapUtils::GetTriangleFromWorldPosition(pos, triangle, height))
            {
                if (Intersect_AABB_TRIANGLE(box, triangle))
                    return true;
            }
        }

        return false;
    }
    bool Intersect_AABB_TERRAIN_SWEEP(const Geometry::AABoundingBox& box, Geometry::Triangle& triangle, const vec3& direction, f32& height, f32 maxDist, f32& outTimeToCollide)
    {
        vec3 offsets[5] =
        {
            {0, 0, 0},
            {-box.extents.x, -box.extents.z, 0},
            {box.extents.x, -box.extents.z, 0},
            {-box.extents.x, box.extents.z, 0},
            {box.extents.x, box.extents.z, 0}
        };


        // TODO: Look into if we want to optimize this, the reason we currently
        //       always get the Verticies from pos is because we don't manually
        //       check for chunk/cell borders, but we could speed this part up
        //       by doing that manually instead of calling GetVerticiesFromWorldPosition
        //       5 times.

        outTimeToCollide = std::numeric_limits<f32>().max();

        for (i32 i = 0; i < 5; i++)
        {
            vec3 pos = box.center + offsets[i];
            Geometry::Triangle tri;

            if (Terrain::MapUtils::GetTriangleFromWorldPosition(pos, tri, height))
            {
                // First store tri in triangle and then translate tri's position so that center is origin(0,0)
                triangle = tri;

                tri.vert1 -= box.center;
                tri.vert2 -= box.center;
                tri.vert3 -= box.center;

                // We need to find the "shortest" collision here and not just "any" collision (Not doing this causes issues when testing against multiple triangles
                f32 tmpTimeToCollision = 0;
                if (Intersect_AABB_TRIANGLE_SWEEP(box.extents, tri, direction, maxDist, tmpTimeToCollision, true))
                {
                    if (tmpTimeToCollision < outTimeToCollide)
                        outTimeToCollide = tmpTimeToCollision;
                }
            }
        }

        return outTimeToCollide != std::numeric_limits<f32>().max();
    }

    bool Intersect_AABB_AABB(const Geometry::AABoundingBox& a, const Geometry::AABoundingBox& b)
    {
        bool x = glm::abs(a.center[0] - b.center[0]) <= (a.extents[0] + b.extents[0]);
        bool y = glm::abs(a.center[1] - b.center[1]) <= (a.extents[1] + b.extents[1]);
        bool z = glm::abs(a.center[2] - b.center[2]) <= (a.extents[2] + b.extents[2]);

        return x && y && z;
    }
    bool Intersect_AABB_SWEEP(const Geometry::AABoundingBox& aabb, const Geometry::AABoundingBox& aabbToCollideWith, const vec3& velocity, f32& t)
    {
        const vec3 scale = vec3(1.0f, 1.0f, 1.0f) / velocity;
        const vec3 sign = glm::sign(scale);
        const f32 nearTimeX = (aabbToCollideWith.center.x - sign.x * (aabbToCollideWith.extents.x + aabb.extents.x) - aabb.center.x) * scale.x;
        const f32 nearTimeY = (aabbToCollideWith.center.y - sign.y * (aabbToCollideWith.extents.y + aabb.extents.y) - aabb.center.y) * scale.y;
        const f32 nearTimeZ = (aabbToCollideWith.center.z - sign.z * (aabbToCollideWith.extents.z + aabb.extents.z) - aabb.center.z) * scale.z;
        const f32 farTimeX = (aabbToCollideWith.center.x + sign.x * (aabbToCollideWith.extents.x + aabb.extents.x) - aabb.center.x) * scale.x;
        const f32 farTimeY = (aabbToCollideWith.center.y + sign.y * (aabbToCollideWith.extents.y + aabb.extents.y) - aabb.center.y) * scale.y;
        const f32 farTimeZ = (aabbToCollideWith.center.z + sign.z * (aabbToCollideWith.extents.z + aabb.extents.z) - aabb.center.z) * scale.z;

        if ((nearTimeX > farTimeY || nearTimeX > farTimeZ) || (nearTimeY > farTimeX || nearTimeY > farTimeZ) || (nearTimeZ > farTimeX || nearTimeZ > farTimeY)) {
            return false;
        }

        const f32 nearTime = glm::max(glm::max(nearTimeX, nearTimeY), nearTimeZ);
        const f32 farTime = glm::min(glm::min(farTimeX, farTimeY), farTimeZ);

        if (nearTime >= 1.0f || farTime <= 0.0f) {
            return false;
        }

        t = glm::clamp(nearTime, 0.0f, 1.0f);
        return true;
    }

    bool Intersect_SPHERE_TRIANGLE(const vec3& spherePos, const f32 sphereRadius, const Geometry::Triangle& triangle)
    {
        // Translate problem so sphere is centered at origin
        vec3 a = triangle.vert1 - spherePos;
        vec3 b = triangle.vert2 - spherePos;
        vec3 c = triangle.vert3 - spherePos;
        f32 rr = sphereRadius * sphereRadius;

        // Compute a vector normal to triangle plane(V), normalize it(N)
        vec3 v = glm::cross(b - a, c - a);

        // Compute distance d of sphere center to triangle plane
        f32 d = glm::dot(a, v);
        f32 e = glm::dot(v, v);

        bool sep1 = d * d > rr * e;

        f32 aa = glm::dot(a, a);
        f32 ab = glm::dot(a, b);
        f32 ac = glm::dot(a, c);
        f32 bb = glm::dot(b, b);
        f32 bc = glm::dot(b, c);
        f32 cc = glm::dot(c, c);

        bool sep2 = (aa > rr) && (ab > aa) && (ac > aa);
        bool sep3 = (bb > rr) && (ab > bb) && (bc > bb);
        bool sep4 = (cc > rr) && (ac > cc) && (bc > cc);

        vec3 AB = b - a;
        vec3 BC = c - b;
        vec3 CA = a - c;

        f32 d1 = ab - aa;
        f32 d2 = bc - bb;
        f32 d3 = ac - cc;
        f32 e1 = glm::dot(AB, AB);
        f32 e2 = glm::dot(BC, BC);
        f32 e3 = glm::dot(CA, CA);

        vec3 q1 = a * e1 - d1 * AB;
        vec3 q2 = b * e2 - d2 * BC;
        vec3 q3 = c * e3 - d3 * CA;

        vec3 qc = c * e1 - q1;
        vec3 qa = a * e2 - q2;
        vec3 qb = b * e3 - q3;

        bool sep5 = (glm::dot(q1, q1) > rr * e1 * e1) && (glm::dot(q1, qc) > 0);
        bool sep6 = (glm::dot(q2, q2) > rr * e2 * e2) && (glm::dot(q2, qa) > 0);
        bool sep7 = (glm::dot(q3, q3) > rr * e3 * e3) && (glm::dot(q3, qb) > 0);

        bool seperated = sep1 || sep2 || sep3 || sep4 || sep5 || sep6 || sep7;
        return !seperated;
    }
    bool CheckCollisionForCModels(Terrain::Map& currentMap, const Movement& srcMovement, const CModelInfo& srcCModelInfo, vec3& triangleNormal, f32& triangleAngle, f32& timeToCollide)
    {
        if (srcMovement.velocity.x == 0.0f && srcMovement.velocity.y == 0.0f && srcMovement.velocity.z == 0.0f)
            return false;

        SafeVector<entt::entity>* collidableEntityList = currentMap.GetCollidableEntityListByChunkID(srcCModelInfo.currentChunkID);
        if (!collidableEntityList)
            return false;

        {
            entt::registry* registry = ServiceLocator::GetGameRegistry();
            TimeSingleton& timeSingleton = registry->ctx<TimeSingleton>();

            ClientRenderer* clientRenderer = ServiceLocator::GetClientRenderer();
            DebugRenderer* debugRenderer = clientRenderer->GetDebugRenderer();
            CModelRenderer* cmodelRenderer = clientRenderer->GetCModelRenderer();

            SafeVectorScopedReadLock<entt::entity> collidableEntityReadLock(*collidableEntityList);
            const std::vector<entt::entity>& collidableEntities = collidableEntityReadLock.Get();

            u32 numCollidableEntities = static_cast<u32>(collidableEntities.size());
            if (numCollidableEntities == 0)
                return false;

            SafeVectorScopedReadLock<CModelRenderer::LoadedComplexModel> loadedComplexModelsReadLock(cmodelRenderer->GetLoadedComplexModels());
            SafeVectorScopedReadLock<CModelRenderer::ModelInstanceData> cmodelInstanceDatasReadLock(cmodelRenderer->GetModelInstanceDatas());
            SafeVectorScopedReadLock<mat4x4> cmodelInstanceMatricesReadLock(cmodelRenderer->GetModelInstanceMatrices());
            SafeVectorScopedReadLock<Geometry::Triangle> collisionTriangleListReadLock(cmodelRenderer->GetCollisionTriangles());

            const std::vector<CModelRenderer::LoadedComplexModel>& loadedComplexModels = loadedComplexModelsReadLock.Get();
            const std::vector<CModelRenderer::ModelInstanceData>& cmodelInstanceDatas = cmodelInstanceDatasReadLock.Get();
            const std::vector<mat4x4>& cmodelInstanceMatrices = cmodelInstanceMatricesReadLock.Get();
            const std::vector<Geometry::Triangle>& collisionTriangles = collisionTriangleListReadLock.Get();

            const CModelRenderer::ModelInstanceData& srcInstanceData = cmodelInstanceDatas[srcCModelInfo.instanceID];
            const CModelRenderer::LoadedComplexModel& srcLoadedComplexModel = loadedComplexModels[srcInstanceData.modelID];

            vec3 velocityThisFrame = static_cast<vec3>(srcMovement.velocity) * timeSingleton.deltaTime;
            Geometry::AABoundingBox srcAABB;
            {
                vec3 center = srcLoadedComplexModel.collisionAABB.center;
                vec3 extents = srcLoadedComplexModel.collisionAABB.extents;

                const mat4x4& m = cmodelInstanceMatrices[srcCModelInfo.instanceID];
                vec3 transformedCenter = vec3(m * vec4(center, 1.0f));

                // Transform extents (take maximum)
                glm::mat3x3 absMatrix = glm::mat3x3(glm::abs(vec3(m[0])), glm::abs(vec3(m[1])), glm::abs(vec3(m[2])));
                vec3 transformedExtents = absMatrix * extents;

                // Transform to min/max box representation
                srcAABB.center = transformedCenter;
                srcAABB.extents = transformedExtents;
            }

            debugRenderer->DrawAABB3D(srcAABB.center, srcAABB.extents, 0xff00ff00);

            // Check for collision
            timeToCollide = std::numeric_limits<f32>().max();
            Geometry::Triangle closestTriangle;
            Geometry::Triangle closestTransformedTriangle;

            for (u32 i = 0; i < numCollidableEntities; i++)
            {
                entt::entity entityID = collidableEntities[i];
                CModelInfo& cmodelInfo = registry->get<CModelInfo>(entityID);

                u32 instanceID = cmodelInfo.instanceID;
                const CModelRenderer::ModelInstanceData& instanceData = cmodelInstanceDatas[cmodelInfo.instanceID];
                const CModelRenderer::LoadedComplexModel& loadedComplexModel = loadedComplexModels[instanceData.modelID];
                const mat4x4& instanceMatrix = cmodelInstanceMatrices[instanceID];

                Geometry::AABoundingBox cmodelAABB;
                {
                    vec3 center = loadedComplexModel.collisionAABB.center;
                    vec3 extents = loadedComplexModel.collisionAABB.extents;

                    vec3 transformedCenter = vec3(instanceMatrix * vec4(center, 1.0f));

                    // Transform extents (take maximum)
                    glm::mat3x3 absMatrix = glm::mat3x3(glm::abs(vec3(instanceMatrix[0])), glm::abs(vec3(instanceMatrix[1])), glm::abs(vec3(instanceMatrix[2])));
                    vec3 transformedExtents = absMatrix * extents;

                    // Transform to min/max box representation
                    cmodelAABB.center = transformedCenter;
                    cmodelAABB.extents = transformedExtents;
                }

                if (!Intersect_AABB_AABB(srcAABB, cmodelAABB))
                {
                    f32 t = 0;
                    if (!Intersect_AABB_SWEEP(srcAABB, cmodelAABB, velocityThisFrame, t))
                        continue;
                }

                debugRenderer->DrawAABB3D(cmodelAABB.center, cmodelAABB.extents, 0xff00ff00);

                u32 triangleOffset = loadedComplexModel.collisionTriangleOffset;
                u32 numTriangles = loadedComplexModel.numCollisionTriangles;

                for (u32 j = 0; j < numTriangles; j++)
                {
                    u32 triangleID = j + triangleOffset;
                    const Geometry::Triangle& triangle = collisionTriangles[triangleID];

                    Geometry::Triangle transformedTriangle;
                    {
                        // Transform Triangle using Instance Matrix
                        transformedTriangle.vert1 = vec3(instanceMatrix * vec4(triangle.vert1, 1.0f));
                        transformedTriangle.vert2 = vec3(instanceMatrix * vec4(triangle.vert2, 1.0f));
                        transformedTriangle.vert3 = vec3(instanceMatrix * vec4(triangle.vert3, 1.0f));

                        // Transform Triangle making it relative to srcAABB
                        transformedTriangle.vert1 -= srcAABB.center;
                        transformedTriangle.vert2 -= srcAABB.center;
                        transformedTriangle.vert3 -= srcAABB.center;
                    }

                    f32 tmpTimeToCollision = 0;
                    if (Intersect_AABB_TRIANGLE_SWEEP(srcAABB.extents, transformedTriangle, velocityThisFrame, 1.0f, tmpTimeToCollision, true))
                    {
                        if (tmpTimeToCollision < timeToCollide)
                        {
                            timeToCollide = tmpTimeToCollision;
                            closestTriangle = triangle;
                            closestTransformedTriangle = transformedTriangle;
                        }
                    }
                }
            }

            if (timeToCollide != std::numeric_limits<f32>().max())
            {
                //timeToCollide -= std::numeric_limits<f32>().epsilon();
                timeToCollide = glm::clamp(timeToCollide, 0.0f, 1.0f);

                triangleNormal = closestTriangle.GetCollisionNormal();
                triangleAngle = closestTriangle.GetCollisionSteepnessAngle();

                closestTransformedTriangle.vert1 += srcAABB.center;
                closestTransformedTriangle.vert2 += srcAABB.center;
                closestTransformedTriangle.vert3 += srcAABB.center;

                debugRenderer->DrawLine3D(closestTransformedTriangle.vert1, closestTransformedTriangle.vert2, 0xff0000ff);
                debugRenderer->DrawLine3D(closestTransformedTriangle.vert2, closestTransformedTriangle.vert3, 0xff0000ff);
                debugRenderer->DrawLine3D(closestTransformedTriangle.vert3, closestTransformedTriangle.vert1, 0xff0000ff);

            }
        }

        return timeToCollide != std::numeric_limits<f32>().max();
    }
#pragma warning(pop)
}