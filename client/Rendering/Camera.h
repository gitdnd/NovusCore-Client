#pragma once
#include <NovusTypes.h>
#include <CVar/CVarSystem.h>

enum class FrustumPlane
{
    Left,
    Right,
    Bottom,
    Top,
    Near,
    Far,
};

struct CameraSaveData
{
    vec3 position;
    f32 yaw;
    f32 pitch;
    f32 movement;
};

constexpr vec3 worldUp = vec3(0, 0, 1);

class Window;
class Camera
{
public:
    Camera();

    virtual void Update(f32 deltaTime, f32 fovInDegrees, f32 aspectRatioWH) = 0;

    void SetWindow(Window* window) { _window = window; }
    Window* GetWindow() { return _window; }

    void SetActive(bool state) 
    { 
        if (state == _active)
            return;

        _active = state;

        if (state)
        {
            Enabled();
        }
        else
        {
            Disabled();
        }
    }
    bool IsActive() { return _active; }

    void SetNearClip(f32 value) { *CVarSystem::Get()->GetFloatCVar("camera.nearClip") = value; }
    f32 GetNearClip() { return static_cast<f32>(*CVarSystem::Get()->GetFloatCVar("camera.nearClip")); }

    void SetFarClip(f32 value) { *CVarSystem::Get()->GetFloatCVar("camera.farClip") = value; }
    f32 GetFarClip() { return static_cast<f32>(*CVarSystem::Get()->GetFloatCVar("camera.farClip")); }

    bool LoadFromFile(std::string filename);
    bool SaveToFile(std::string filename);

    __forceinline const mat4x4& GetViewMatrix() const { return _viewMatrix; }
    __forceinline const mat4x4& GetProjectionMatrix() const { return _projectionMatrix; }
    __forceinline const mat4x4& GetViewProjectionMatrix() const { return _viewProjectionMatrix; }
    __forceinline const vec4* GetFrustumPlanes() const { return _frustumPlanes; }

    void SetPosition(vec3 position)
    {
        _position = position;
    }
    vec3 GetPosition() const { return _position; }
    
    void SetPreviousMousePosition(vec2 position)
    {
        _prevMousePosition = position;
    }
    vec2 GetPreviousMousePosition() const { return _prevMousePosition; }

    void SetYaw(f32 value) { _yaw = value; }
    f32 GetYaw() { return _yaw; }
    void SetPitch(f32 value) { _pitch = value; }
    f32 GetPitch() { return _pitch; }
    vec3 GetRotation() const { return vec3(0.0f, _pitch, _yaw); }

    f32 GetFOVInDegrees() { return _fovInDegrees; }
    f32 GetAspectRatio() { return _aspectRatio; }

    void SetMouseCaptured(bool state) { _captureMouse = state; }
    bool IsMouseCaptured() const { return _captureMouse; }

    void SetCapturedMouseMoved(bool state) { _captureMouseHasMoved = state; }
    bool GetCapturedMouseMoved() const { return _captureMouseHasMoved; }

protected:
    virtual void Init() = 0;
    virtual void Enabled() = 0;
    virtual void Disabled() = 0;

    void UpdateCameraVectors();
    void UpdateFrustumPlanes();

protected:
    Window* _window = nullptr;

    bool _active = false;
    f32 _nearClip = 1.0f;
    f32 _farClip = 100000.0f;
    f32 _fovInDegrees = 75.0f;
    f32 _aspectRatio = 1.0f;

    vec3 _position = vec3(0, 0, 0);
    
    // Euler Angles
    f32 _yaw = 0.0f;
    f32 _pitch = 0.0f;

    // Rotation Matrix
    mat4x4 _rotationMatrix = mat4x4();
    mat4x4 _viewMatrix = mat4x4();
    mat4x4 _projectionMatrix = mat4x4();
    mat4x4 _viewProjectionMatrix = mat4x4();

    // Direction vectors
    vec3 _front = vec3(0, 0, 0);
    vec3 _up = vec3(0, 0, 0);
    vec3 _left = vec3(0, 0, 0);

    // Mouse States
    vec2 _prevMousePosition = vec2(0, 0);
    bool _captureMouse = false;
    bool _captureMouseHasMoved = false;
    f32 _movementSpeed = 50.0f;
    f32 _mouseSensitivity = 0.05f;

    vec4 _frustumPlanes[6] = 
    { 
        vec4(0, 0, 0, 0), vec4(0, 0, 0, 0), vec4(0, 0, 0, 0),
        vec4(0, 0, 0, 0), vec4(0, 0, 0, 0), vec4(0, 0, 0, 0) 
    };
};