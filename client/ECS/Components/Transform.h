#pragma once
#include <NovusTypes.h>
#include <glm/matrix.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/euler_angles.hpp>

struct Transform
{
    vec3 position = vec3(0.f, 0.f, 0.f);
    vec3 velocity = vec3(0.f, 0.f, 0.f);
    vec3 scale = vec3(1.f, 1.f, 1.f);

    f32 moveSpeed = 7.1111f;
    f32 fallSpeed = 19.5f;
    f32 fallAcceleration = 7.33f;

    struct MovementFlags
    {
        union
        {
            struct
            {
                u8 FORWARD : 1;
                u8 BACKWARD : 1;
                u8 LEFT : 1;
                u8 RIGHT : 1;
                u8 GROUNDED : 1;
            };

            u16 value;
        };

    } movementFlags;

    // Rotation
    f32 yaw = 0.0f;
    f32 yawBase = 0.0f;
    f32 yawOffset = 0.0f;

    f32 pitch = 0.0f;

    // Direction vectors
    vec3 front = vec3(0.f, 0.f, 0.f);
    vec3 up = vec3(0.f, 0.f, 0.f);
    vec3 left = vec3(0.f, 0.f, 0.f);

    vec3 GetRotation() const { return vec3(pitch, 0.0f, yaw); }
    mat4x4 GetRotationMatrix() 
    {
        glm::quat rotQuat = glm::quat(glm::vec3(0.0f, glm::radians(pitch), glm::radians(yaw)));
        return glm::mat4_cast(rotQuat);
    }

    mat4x4 GetMatrix()
    {
        // When we pass 1 into the constructor, it will construct an identity matrix
        mat4x4 matrix(1);

        // Order is important here (Go lookup how matrices work if confused)
        matrix = glm::translate(matrix, position);
        matrix *= GetRotationMatrix();
        matrix = glm::scale(matrix, scale);

        return matrix;
    }

    void UpdateDirectionVectors()
    {
        glm::quat rotQuat = glm::quat(glm::vec3(0.0f, glm::radians(pitch), glm::radians(yawBase)));
        mat4x4 rotationMatrix = glm::mat4_cast(rotQuat);

        front = rotationMatrix[0];
        up = rotationMatrix[2];
        left = rotationMatrix[1];
    }
};

struct TransformIsDirty 
{
    TransformIsDirty() {}
}; // Empty struct