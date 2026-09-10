#pragma once

#include "editor_scene.h"

#include <iryven/iryven.h>

#include <cstdint>
#include <string>
#include <vector>

class EditorLayer final : public Iryven::Layer {
public:
    explicit EditorLayer(Iryven::Engine& engine);
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(float deltaTime) override;
    void OnImGuiRender() override;

private:
    enum class PendingComponent {
        None,
        Transform,
        Camera,
        Light,
        RigidBody,
        Collider,
        MeshRenderer,
        UIText,
    };

    enum class PendingEntityPreset {
        None,
        Empty,
        Cube,
        Sphere,
        Cylinder,
        Cone,
        Plane,
        Camera,
        DirectionalLight,
        PointLight,
        UIText,
        DynamicCube,
    };

    struct EntitySnapshot {
        std::uint64_t entityId = 0;
        std::uint64_t backupId = 0;
        std::string name;
    };

    void DrawPanels();
    void Navigate();
    void FocusSelection();
    bool SaveScene();
    void CreateEntityFromPreset(PendingEntityPreset preset);
    void ApplyPendingActions();
    void DrawAddComponentMenu();
    void DrawCreateEntityMenu();
    void TogglePlayMode();
    void EnterPlayMode();
    void ExitPlayMode();
    void CapturePlaySnapshot();
    void RestorePlaySnapshot();
    const std::filesystem::path scenePath_{"scenes/editor.json"};
    std::string sceneStatus_{"Ctrl+S to save scene"};
    bool saveFailed_ = false;

    Iryven::Engine& engine_;
    Iryven::Entity camera_;
    Iryven::Entity selected_;
    glm::vec3 inspectorAngles_{0.0f};
    glm::vec3 velocity_{0.0f};
    glm::vec2 lookVelocity_{0.0f};
    float moveSpeed_ = 5.0f;
    float lookSpeed_ = 60.0f;
    float sensitivity_ = 0.15f;
    float moveResponse_ = 12.0f;
    float lookResponse_ = 16.0f;
    bool navigating_ = false;
    bool playToggleRequested_ = false;
    bool playing_ = false;
    std::vector<EntitySnapshot> playSnapshot_;
    std::string playSelectedEntity_;
    std::string playCameraEntity_;
    PendingEntityPreset pendingEntityPreset_ = PendingEntityPreset::None;
    PendingComponent pendingComponent_ = PendingComponent::None;
};
