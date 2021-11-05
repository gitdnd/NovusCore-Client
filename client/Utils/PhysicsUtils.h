#pragma once
#include <entity/fwd.hpp>

namespace Geometry
{
    struct Triangle;
    struct AABoundingBox;
}

namespace Terrain
{
    struct Map;
}

struct Transform;
struct CModelInfo;

namespace PhysicsUtils
{
    void Project(const vec3& vertex, const vec3& axis, vec2& minMax);
    void ProjectTriangle(const Geometry::Triangle& triangle, const vec3& axis, vec2& minMax);
    void ProjectBox(const Geometry::AABoundingBox& box, const vec3& axis, vec2& minMax);

    bool TestOverlap(const f32& boxExt, bool& validMTD, const f32& triMin, const f32& triMax, f32& d0, f32& d1);
    i32 TestAxis(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, const vec3& axis, bool& validMTD, f32& tFirst, f32& tLast);
    i32 TestAxisXYZ(const i32 index, const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, const f32& oneOverDir, bool& validMTD, f32& tFirst, f32& tLast);
    bool TestSeperationAxes(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& normal, const vec3& dir, const vec3& oneOverDir, f32 tmax, f32& t);

    bool Intersect_AABB_TRIANGLE(const Geometry::AABoundingBox& box, const Geometry::Triangle& triangle);
    bool Intersect_AABB_TRIANGLE_SWEEP(const vec3& boxScale, const Geometry::Triangle& triangle, const vec3& dir, f32 maxDist, f32& outDistToCollision, bool backFaceCulling);
    bool Intersect_AABB_TERRAIN(const vec3& position, const Geometry::AABoundingBox& box, Geometry::Triangle& triangle, f32& height);
    bool Intersect_AABB_TERRAIN_SWEEP(const Geometry::AABoundingBox& box, Geometry::Triangle& triangle, const vec3& direction, f32& height, f32 maxDist, f32& outTimeToCollide);
    bool Intersect_AABB_AABB(const Geometry::AABoundingBox& a, const Geometry::AABoundingBox& b);
    bool Intersect_AABB_SWEEP(const Geometry::AABoundingBox& aabb, const Geometry::AABoundingBox& aabbToCollideWith, const vec3& velocity, f32& t);
    bool Intersect_SPHERE_TRIANGLE(const vec3& spherePos, const f32 sphereRadius, const Geometry::Triangle& triangle);

    bool CheckCollisionForCModels(Terrain::Map& currentMap, const Transform& srcTransform, const CModelInfo& srcCModelInfo, vec3& triangleNormal, f32& triangleAngle, f32& timeToCollide);
}