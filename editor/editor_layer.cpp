#include "editor_layer.h"
#include "editor_scene.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

EditorLayer::EditorLayer(Iryven::Engine& engine) : Layer("Editor"), engine_(engine) {}

void EditorLayer::OnAttach()
{
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FontSizeBase = 18.0f;
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {18, 16};
    style.FramePadding = {10, 7};
    style.ItemSpacing = {10, 10};
    style.ItemInnerSpacing = {7, 6};
    style.WindowRounding = 0;
    style.FrameRounding = 5;
    style.GrabRounding = 4;
    style.ScrollbarRounding = 6;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    auto* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = {0.075f, 0.085f, 0.105f, 1};
    colors[ImGuiCol_Text] = {0.88f, 0.91f, 0.95f, 1};
    colors[ImGuiCol_TextDisabled] = {0.46f, 0.52f, 0.61f, 1};
    colors[ImGuiCol_Border] = {0.17f, 0.20f, 0.25f, 1};
    colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
    colors[ImGuiCol_FrameBg] = {0.12f, 0.14f, 0.18f, 1};
    colors[ImGuiCol_FrameBgHovered] = {0.17f, 0.21f, 0.27f, 1};
    colors[ImGuiCol_FrameBgActive] = {0.19f, 0.27f, 0.34f, 1};
    colors[ImGuiCol_Header] = {0.12f, 0.25f, 0.29f, 1};
    colors[ImGuiCol_HeaderHovered] = {0.16f, 0.30f, 0.34f, 1};
    colors[ImGuiCol_HeaderActive] = {0.18f, 0.36f, 0.39f, 1};
    colors[ImGuiCol_Button] = {0.15f, 0.25f, 0.29f, 1};
    colors[ImGuiCol_ButtonHovered] = {0.18f, 0.35f, 0.39f, 1};
    colors[ImGuiCol_ButtonActive] = {0.20f, 0.43f, 0.46f, 1};
    colors[ImGuiCol_CheckMark] = colors[ImGuiCol_SliderGrab] = {0.38f, 0.79f, 0.72f, 1};
    colors[ImGuiCol_SliderGrabActive] = {0.55f, 0.92f, 0.83f, 1};

    engine_.GetGameLayer().SetSimulationEnabled(false);
    auto& world = engine_.GetWorld();
    auto scene = CreateEditorStarterScene(world);
    camera_ = scene.camera;
    selected_ = scene.cube;

    if (std::filesystem::exists(scenePath_)) {
        LoadEditorScene(world, scenePath_, scene);
        camera_ = scene.camera;
        selected_ = scene.cube;
        inspectorAngles_ = glm::degrees(glm::eulerAngles(selected_.Get<Iryven::Transform>().rotation));
        sceneStatus_ = "Loaded scenes/editor.json";
    }
}
void EditorLayer::OnDetach()
{
    engine_.GetGameLayer().SetSimulationEnabled(false);
    if (navigating_) glfwSetInputMode(static_cast<GLFWwindow*>(engine_.GetWindow().GetNativeHandle()), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void EditorLayer::OnUpdate(float)
{
    ApplyPendingActions();
}

void EditorLayer::OnImGuiRender()
{
    DrawPanels();
    Navigate();
    if (!playing_ && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) SaveScene();
}

bool EditorLayer::SaveScene()
{
    try {
        std::filesystem::create_directories(scenePath_.parent_path());
        engine_.GetWorld().SerializeScene(scenePath_);
        sceneStatus_ = "Saved scenes/editor.json";
        saveFailed_ = false;
        return true;
    } catch (const std::exception& error) {
        sceneStatus_ = error.what();
        saveFailed_ = true;
        return false;
    }
}

void EditorLayer::CreateEntityFromPreset(PendingEntityPreset preset)
{
    auto& world = engine_.GetWorld();
    const char* baseName = "Entity";
    switch (preset) {
    case PendingEntityPreset::Cube: baseName = "Cube"; break;
    case PendingEntityPreset::Sphere: baseName = "Sphere"; break;
    case PendingEntityPreset::Cylinder: baseName = "Cylinder"; break;
    case PendingEntityPreset::Cone: baseName = "Cone"; break;
    case PendingEntityPreset::Plane: baseName = "Plane"; break;
    case PendingEntityPreset::Camera: baseName = "Camera"; break;
    case PendingEntityPreset::DirectionalLight: baseName = "Directional Light"; break;
    case PendingEntityPreset::PointLight: baseName = "Point Light"; break;
    case PendingEntityPreset::UIText: baseName = "UI Text"; break;
    case PendingEntityPreset::DynamicCube: baseName = "Dynamic Cube"; break;
    case PendingEntityPreset::Empty:
    case PendingEntityPreset::None:
        break;
    }

    std::string name = baseName;
    for (int suffix = 2; world.GetFlecsWorld().lookup(name.c_str()).id() != 0; ++suffix)
        name = std::string(baseName) + " " + std::to_string(suffix);

    selected_ = world.CreateEntity(name);
    const auto addRenderable = [this](EditorPrimitiveMesh primitive) {
        selected_.Add<Iryven::Transform>();
        selected_.Add<EditorRenderable>(EditorRenderable{ .primitive = primitive });
        ApplyEditorRenderable(selected_);
    };
    switch (preset) {
    case PendingEntityPreset::Cube: addRenderable(EditorPrimitiveMesh::Cube); break;
    case PendingEntityPreset::Sphere: addRenderable(EditorPrimitiveMesh::Sphere); break;
    case PendingEntityPreset::Cylinder: addRenderable(EditorPrimitiveMesh::Cylinder); break;
    case PendingEntityPreset::Cone: addRenderable(EditorPrimitiveMesh::Cone); break;
    case PendingEntityPreset::Plane: addRenderable(EditorPrimitiveMesh::Plane); break;
    case PendingEntityPreset::Camera:
        selected_.Add<Iryven::Transform>();
        selected_.Add<Iryven::Camera>(Iryven::Camera{ .primary = false });
        break;
    case PendingEntityPreset::DirectionalLight:
        selected_.Add<Iryven::Transform>();
        selected_.Add<Iryven::Light>();
        break;
    case PendingEntityPreset::PointLight:
        selected_.Add<Iryven::Transform>();
        selected_.Add<Iryven::Light>(Iryven::Light{ .type = Iryven::LightType::Point, .intensity = 10.0f });
        break;
    case PendingEntityPreset::UIText:
        selected_.Add<Iryven::UIText>();
        break;
    case PendingEntityPreset::DynamicCube:
        addRenderable(EditorPrimitiveMesh::Cube);
        selected_.Add<Iryven::RigidBody>(Iryven::RigidBody{ .type = Iryven::BodyType::Dynamic });
        selected_.Add<Iryven::Collider>(Iryven::Collider::Box(glm::vec3(0.5f)));
        break;
    case PendingEntityPreset::Empty:
    case PendingEntityPreset::None:
        break;
    }
    inspectorAngles_ = glm::vec3(0.0f);
    sceneStatus_ = "Created " + name;
    saveFailed_ = false;
}

void EditorLayer::ApplyPendingActions()
{
    if (playToggleRequested_) {
        playToggleRequested_ = false;
        TogglePlayMode();
    }

    if (playing_) {
        pendingEntityPreset_ = PendingEntityPreset::None;
        pendingComponent_ = PendingComponent::None;
        return;
    }

    if (pendingEntityPreset_ != PendingEntityPreset::None) {
        const PendingEntityPreset preset = pendingEntityPreset_;
        pendingEntityPreset_ = PendingEntityPreset::None;
        CreateEntityFromPreset(preset);
    }

    if (pendingComponent_ == PendingComponent::None) return;
    const PendingComponent component = pendingComponent_;
    pendingComponent_ = PendingComponent::None;
    if (!selected_.IsAlive()) return;

    switch (component) {
    case PendingComponent::Transform:
        if (!selected_.Has<Iryven::Transform>()) selected_.Add<Iryven::Transform>();
        break;
    case PendingComponent::Camera:
        if (!selected_.Has<Iryven::Camera>()) selected_.Add<Iryven::Camera>();
        break;
    case PendingComponent::Light:
        if (!selected_.Has<Iryven::Light>()) selected_.Add<Iryven::Light>();
        break;
    case PendingComponent::RigidBody:
        if (!selected_.Has<Iryven::RigidBody>()) selected_.Add<Iryven::RigidBody>();
        break;
    case PendingComponent::Collider:
        if (!selected_.Has<Iryven::Collider>()) selected_.Add<Iryven::Collider>();
        break;
    case PendingComponent::MeshRenderer:
        if (!selected_.Has<Iryven::MeshRenderer>()) {
            if (!selected_.Has<EditorRenderable>()) selected_.Add<EditorRenderable>();
            ApplyEditorRenderable(selected_);
        }
        break;
    case PendingComponent::UIText:
        if (!selected_.Has<Iryven::UIText>()) selected_.Add<Iryven::UIText>();
        break;
    case PendingComponent::None:
        break;
    }
}

void EditorLayer::TogglePlayMode()
{
    if (playing_) ExitPlayMode();
    else EnterPlayMode();
}

void EditorLayer::EnterPlayMode()
{
    if (!SaveScene()) return;
    CapturePlaySnapshot();
    engine_.GetGameLayer().SetSimulationEnabled(true);
    playing_ = true;
    sceneStatus_ = "Play mode - simulation is running";
    saveFailed_ = false;
}

void EditorLayer::ExitPlayMode()
{
    engine_.GetGameLayer().SetSimulationEnabled(false);
    RestorePlaySnapshot();
    inspectorAngles_ = selected_.IsAlive() && selected_.Has<Iryven::Transform>()
        ? glm::degrees(glm::eulerAngles(selected_.Get<Iryven::Transform>().rotation))
        : glm::vec3(0.0f);
    velocity_ = glm::vec3(0.0f);
    lookVelocity_ = glm::vec2(0.0f);
    playing_ = false;
    sceneStatus_ = "Stopped - pre-play values restored";
    saveFailed_ = false;
}

void EditorLayer::CapturePlaySnapshot()
{
    playSnapshot_.clear();
    auto& world = engine_.GetWorld();
    world.ForEachEntity([this](Iryven::Entity entity) {
        playSnapshot_.push_back(EntitySnapshot{
            .entityId = entity.GetId(),
            .name = entity.GetName() ? entity.GetName() : "",
        });
    });
    for (auto& snapshot : playSnapshot_) {
        auto source = world.GetFlecsWorld().entity(snapshot.entityId);
        auto backup = source.clone(true);
        backup.add(flecs::Prefab);
        snapshot.backupId = backup.id();
    }
    playSelectedEntity_ = selected_.IsAlive() && selected_.GetName() ? selected_.GetName() : "";
    playCameraEntity_ = camera_.IsAlive() && camera_.GetName() ? camera_.GetName() : "";
}

void EditorLayer::RestorePlaySnapshot()
{
    auto& world = engine_.GetWorld();
    auto& flecsWorld = world.GetFlecsWorld();
    std::unordered_set<std::uint64_t> snapshotEntities;
    snapshotEntities.reserve(playSnapshot_.size());
    for (const auto& snapshot : playSnapshot_) snapshotEntities.insert(snapshot.entityId);

    std::vector<std::uint64_t> runtimeEntities;
    world.ForEachEntity([&](Iryven::Entity entity) {
        if (!snapshotEntities.contains(entity.GetId())) runtimeEntities.push_back(entity.GetId());
    });
    for (const auto entityId : runtimeEntities) {
        auto entity = flecsWorld.entity(entityId);
        if (entity.is_alive()) entity.destruct();
    }

    for (const auto& snapshot : playSnapshot_) {
        auto backup = flecsWorld.entity(snapshot.backupId);
        if (!backup.is_alive()) continue;

        auto entity = flecsWorld.is_alive(snapshot.entityId)
            ? flecsWorld.entity(snapshot.entityId)
            : flecsWorld.entity(snapshot.name.c_str());
        entity.clear();
        backup.clone(true, entity.id());
        entity.remove(flecs::Prefab);
        entity.set_name(snapshot.name.c_str());
    }

    const auto camera = world.GetFlecsWorld().lookup(playCameraEntity_.c_str());
    if (camera.id() != 0) camera_ = Iryven::Entity{camera};
    const auto selected = world.GetFlecsWorld().lookup(playSelectedEntity_.c_str());
    selected_ = selected.id() != 0 ? Iryven::Entity{selected} : camera_;

    world.ResetPhysics();
    for (const auto& snapshot : playSnapshot_) {
        auto backup = flecsWorld.entity(snapshot.backupId);
        if (backup.is_alive()) backup.destruct();
    }
    playSnapshot_.clear();
    playSelectedEntity_.clear();
    playCameraEntity_.clear();
}

namespace {
void SectionLabel(const char* label)
{
    ImGui::Spacing();
    ImGui::TextDisabled("%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

bool VectorField(const char* label, glm::vec3& value, float speed, bool positive = false)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const char* axes[] = {"X", "Y", "Z"};
    const ImVec4 colors[] = {{0.90f,0.43f,0.46f,1}, {0.43f,0.78f,0.58f,1}, {0.43f,0.64f,0.94f,1}};
    bool changed = false;
    if (ImGui::BeginTable("axes", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (int i = 0; i < 3; ++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::TextColored(colors[i], "%s", axes[i]);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat("##value", &value[i], speed,
                positive ? 0.001f : 0.0f, positive ? 1000.0f : 0.0f,
                "%.2f", positive ? ImGuiSliderFlags_AlwaysClamp : 0);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}

bool SliderField(const char* label, float& value, float min, float max, const char* format = "%.1f")
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    const bool changed = ImGui::SliderFloat("##value", &value, min, max, format);
    ImGui::PopID();
    return changed;
}

void DrawMeshRenderer(Iryven::Entity entity)
{
    if (!entity.Has<EditorRenderable>()) {
        ImGui::TextDisabled("Custom model assets are read-only in this inspector");
        return;
    }

    auto& settings = entity.Get<EditorRenderable>();
    const auto& choices = EditorPrimitiveChoices();
    int selectedPrimitive = -1;
    for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
        if (settings.primitive == choices[index].primitive) {
            selectedPrimitive = index;
            break;
        }
    }

    bool changed = false;
    const char* preview = selectedPrimitive >= 0 ? choices[selectedPrimitive].name : "Custom mesh";
    ImGui::TextUnformatted("Mesh");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##mesh", preview)) {
        for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
            const bool selected = selectedPrimitive == index;
            if (ImGui::Selectable(choices[index].name, selected)) {
                settings.primitive = choices[index].primitive;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Material");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::ColorEdit4("Base color", glm::value_ptr(settings.baseColor.value));
    changed |= SliderField("Roughness", settings.roughness, 0.0f, 1.0f, "%.2f");
    changed |= SliderField("Metallic", settings.metallic, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::ColorEdit3("Emissive", glm::value_ptr(settings.emissive.value));
    changed |= SliderField("Normal strength", settings.normalScale, 0.0f, 2.0f, "%.2f");
    changed |= SliderField("Occlusion strength", settings.occlusionStrength, 0.0f, 1.0f, "%.2f");
    if (changed) ApplyEditorRenderable(entity);
}
}

void EditorLayer::DrawPanels()
{
    const auto size = ImGui::GetIO().DisplaySize;
    const float left = std::min(270.0f, size.x * 0.24f);
    const float right = std::min(360.0f, size.x * 0.32f);
    const float top = 62.0f;
    const float bottom = 36.0f;
    const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({size.x, top});
    ImGui::Begin("##toolbar", nullptr, flags | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored({0.48f, 0.85f, 0.77f, 1}, "I R Y V E N");
    ImGui::SameLine();
    ImGui::TextDisabled("  /  SCENE EDITOR");
    if (size.x > 900) {
        ImGui::SameLine(left + 24);
        ImGui::TextUnformatted("Starter scene");
        ImGui::SameLine();
        ImGui::TextDisabled("  Perspective");
    }
    ImGui::SameLine(size.x - 116.0f);
    if (playing_) ImGui::PushStyleColor(ImGuiCol_Button, {0.52f, 0.16f, 0.18f, 1.0f});
    if (ImGui::Button(playing_ ? "Stop" : "Play", {92, 34})) playToggleRequested_ = true;
    if (playing_) ImGui::PopStyleColor();
    ImGui::End();

    ImGui::SetNextWindowPos({0, top});
    ImGui::SetNextWindowSize({left, std::max(1.0f, size.y - top - bottom)});
    ImGui::Begin("##hierarchy", nullptr, flags);
    SectionLabel("HIERARCHY");
    ImGui::TextDisabled("Scene objects");
    ImGui::Spacing();
    ImGui::BeginDisabled(playing_);
    if (ImGui::Button("+  Create entity", {-1, 34})) ImGui::OpenPopup("create_entity");
    DrawCreateEntityMenu();
    ImGui::EndDisabled();
    ImGui::Spacing();
    engine_.GetWorld().ForEachEntity([this](Iryven::Entity entity) {
        ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(entity.GetId())));
        const char* type = entity.Has<Iryven::Camera>() ? "CAM" : entity.Has<Iryven::Light>() ? "LGT" :
            entity.Has<Iryven::MeshRenderer>() ? "OBJ" : "ENT";
        ImGui::TextDisabled("%s", type);
        ImGui::SameLine();
        if (ImGui::Selectable(entity.GetName() ? entity.GetName() : "Unnamed",
            selected_.IsAlive() && selected_.GetId() == entity.GetId(), 0, {0, 28})) {
            selected_ = entity;
            inspectorAngles_ = entity.Has<Iryven::Transform>()
                ? glm::degrees(glm::eulerAngles(entity.Get<Iryven::Transform>().rotation))
                : glm::vec3(0.0f);
        }
        ImGui::PopID();
    });
    SectionLabel("NAVIGATION");
    ImGui::TextWrapped("Right mouse   Look around\nW A S D         Move\nQ / E              Down / Up\nShift               Boost\nF                     Focus selection");
    ImGui::End();

    ImGui::SetNextWindowPos({size.x - right, top});
    ImGui::SetNextWindowSize({right, std::max(1.0f, size.y - top - bottom)});
    ImGui::Begin("##inspector", nullptr, flags);
    SectionLabel("INSPECTOR");
    if (selected_.IsAlive()) {
        ImGui::TextUnformatted(selected_.GetName() ? selected_.GetName() : "Unnamed");
        ImGui::TextDisabled(playing_ ? "Runtime entity (read-only)" : "Selected entity");
        ImGui::Spacing();
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("+  Add component", {-1, 34})) ImGui::OpenPopup("add_component");
        DrawAddComponentMenu();
        ImGui::Spacing();
        if (selected_.Has<Iryven::Transform>() && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& transform = selected_.Get<Iryven::Transform>();
            VectorField("Position", transform.position, 0.05f);
            if (selected_.GetId() == camera_.GetId() && navigating_)
                inspectorAngles_ = glm::degrees(glm::eulerAngles(transform.rotation));
            if (VectorField("Rotation / degrees", inspectorAngles_, 0.5f))
                transform.rotation = glm::normalize(glm::quat(glm::radians(inspectorAngles_)));
            if (selected_.GetId() != camera_.GetId()) VectorField("Scale", transform.scale, 0.02f, true);
            ImGui::Spacing();
            ImGui::BeginDisabled(selected_.GetId() == camera_.GetId());
            if (ImGui::Button("Focus selected", {-1, 34})) FocusSelection();
            ImGui::EndDisabled();
        }
        if (selected_.Has<Iryven::Light>() && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& light = selected_.Get<Iryven::Light>();
            ImGui::Checkbox("Enabled", &light.enabled);
            ImGui::SetNextItemWidth(-1);
            ImGui::ColorEdit3("##lightColor", glm::value_ptr(light.color.value));
            SliderField("Intensity", light.intensity, 0, 100, "%.2f");
        }
        if (selected_.Has<Iryven::Camera>() && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& camera = selected_.Get<Iryven::Camera>();
            ImGui::Checkbox("Primary", &camera.primary);
            SliderField("Field of view", camera.verticalFov, 25, 100, "%.0f deg");
        }
        if (selected_.Has<Iryven::RigidBody>() && ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& body = selected_.Get<Iryven::RigidBody>();
            const char* bodyTypes[] = {"Static", "Kinematic", "Dynamic"};
            int bodyType = static_cast<int>(body.type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##bodyType", &bodyType, bodyTypes, IM_ARRAYSIZE(bodyTypes)))
                body.type = static_cast<Iryven::BodyType>(bodyType);
            SliderField("Gravity scale", body.gravityScale, 0, 5, "%.2f");
            ImGui::Checkbox("Fixed rotation", &body.fixedRotation);
        }
        if (selected_.Has<Iryven::Collider>() && ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& collider = selected_.Get<Iryven::Collider>();
            const char* colliderTypes[] = {"Box", "Sphere", "Capsule"};
            int colliderType = static_cast<int>(collider.type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##colliderType", &colliderType, colliderTypes, IM_ARRAYSIZE(colliderTypes)))
                collider.type = static_cast<Iryven::ColliderType>(colliderType);
            if (collider.type == Iryven::ColliderType::Box)
                VectorField("Half extents", collider.halfExtents, 0.02f, true);
            else {
                SliderField("Radius", collider.radius, 0.01f, 20, "%.2f");
                if (collider.type == Iryven::ColliderType::Capsule)
                    SliderField("Half height", collider.halfHeight, 0.01f, 20, "%.2f");
            }
            ImGui::Checkbox("Sensor", &collider.sensor);
        }
        if (selected_.Has<Iryven::MeshRenderer>() && ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawMeshRenderer(selected_);
        }
        if (selected_.Has<Iryven::UIText>() && ImGui::CollapsingHeader("UI Text", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& text = selected_.Get<Iryven::UIText>();
            SliderField("Font size", text.fontSize, 6, 144, "%.0f px");
            ImGui::TextDisabled("Assign a font and text from code");
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextWrapped("Select an entity in the hierarchy to inspect its properties.");
    }
    SectionLabel("EDITOR SETTINGS");
    if (ImGui::CollapsingHeader("Viewport camera")) {
        ImGui::TextDisabled("Independent of selection");
        SliderField("Move speed", moveSpeed_, 0.5f, 30);
        SliderField("Arrow look speed", lookSpeed_, 10, 180);
        SliderField("Mouse sensitivity", sensitivity_, 0.02f, 0.5f, "%.2f");
        SliderField("Movement response", moveResponse_, 2, 30);
        SliderField("Look response", lookResponse_, 2, 30);
        SliderField("Field of view", camera_.Get<Iryven::Camera>().verticalFov, 25, 100, "%.0f deg");
        ImGui::TextDisabled("Lower response = softer motion");
    }
    ImGui::End();

    ImGui::SetNextWindowPos({0, size.y - bottom});
    ImGui::SetNextWindowSize({size.x, bottom});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18, 8});
    ImGui::Begin("##status", nullptr, flags | ImGuiWindowFlags_NoScrollbar);
    const ImVec4 statusColor = playing_
        ? ImVec4{0.48f, 0.85f, 0.77f, 1.0f}
        : saveFailed_ ? ImVec4{0.95f, 0.45f, 0.45f, 1.0f} : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImGui::TextColored(statusColor,
        "%s", saveFailed_ ? "Save failed - hover for details" : sceneStatus_.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sceneStatus_.c_str());
    if (size.x > 700) {
        ImGui::SameLine(size.x - 220);
        ImGui::TextColored({0.48f, 0.85f, 0.77f, 1}, "%.0f FPS", ImGui::GetIO().Framerate);
        ImGui::SameLine();
        ImGui::TextDisabled(" / %.1f ms", 1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorLayer::DrawCreateEntityMenu()
{
    if (!ImGui::BeginPopup("create_entity")) return;

    if (ImGui::MenuItem("Empty entity")) pendingEntityPreset_ = PendingEntityPreset::Empty;
    ImGui::SeparatorText("3D primitives");
    if (ImGui::MenuItem("Cube")) pendingEntityPreset_ = PendingEntityPreset::Cube;
    if (ImGui::MenuItem("Sphere")) pendingEntityPreset_ = PendingEntityPreset::Sphere;
    if (ImGui::MenuItem("Cylinder")) pendingEntityPreset_ = PendingEntityPreset::Cylinder;
    if (ImGui::MenuItem("Cone")) pendingEntityPreset_ = PendingEntityPreset::Cone;
    if (ImGui::MenuItem("Plane")) pendingEntityPreset_ = PendingEntityPreset::Plane;
    ImGui::SeparatorText("Scene");
    if (ImGui::MenuItem("Camera")) pendingEntityPreset_ = PendingEntityPreset::Camera;
    if (ImGui::MenuItem("Directional light")) pendingEntityPreset_ = PendingEntityPreset::DirectionalLight;
    if (ImGui::MenuItem("Point light")) pendingEntityPreset_ = PendingEntityPreset::PointLight;
    if (ImGui::MenuItem("UI text")) pendingEntityPreset_ = PendingEntityPreset::UIText;
    ImGui::SeparatorText("Physics");
    if (ImGui::MenuItem("Dynamic cube")) pendingEntityPreset_ = PendingEntityPreset::DynamicCube;

    ImGui::EndPopup();
}

void EditorLayer::DrawAddComponentMenu()
{
    if (!ImGui::BeginPopup("add_component")) return;

    bool hasAvailableComponent = false;
    const auto addDefault = [this, &hasAvailableComponent]<typename Component>(const char* label, PendingComponent component) {
        if (selected_.Has<Component>()) return;
        hasAvailableComponent = true;
        if (ImGui::MenuItem(label)) pendingComponent_ = component;
    };

    addDefault.template operator()<Iryven::Transform>("Transform", PendingComponent::Transform);
    addDefault.template operator()<Iryven::Camera>("Camera", PendingComponent::Camera);
    addDefault.template operator()<Iryven::Light>("Light", PendingComponent::Light);
    addDefault.template operator()<Iryven::RigidBody>("Rigid Body", PendingComponent::RigidBody);
    addDefault.template operator()<Iryven::Collider>("Collider", PendingComponent::Collider);
    if (!selected_.Has<Iryven::MeshRenderer>()) {
        hasAvailableComponent = true;
        if (ImGui::MenuItem("Mesh Renderer")) pendingComponent_ = PendingComponent::MeshRenderer;
    }
    addDefault.template operator()<Iryven::UIText>("UI Text", PendingComponent::UIText);

    if (!hasAvailableComponent) ImGui::TextDisabled("All components have been added");
    ImGui::EndPopup();
}

void EditorLayer::FocusSelection()
{
    if (!selected_.IsAlive() || selected_.GetId() == camera_.GetId()) return;
    const auto& target = selected_.Get<Iryven::Transform>();
    auto& camera = camera_.Get<Iryven::Transform>();
    const float distance = std::max(3.0f, glm::length(target.scale) * 2.0f);
    camera.position = target.position - camera.rotation * glm::vec3(0, 0, -distance);
    velocity_ = glm::vec3(0);
}

void EditorLayer::Navigate()
{
    auto& io = ImGui::GetIO();
    auto* window = static_cast<GLFWwindow*>(engine_.GetWindow().GetNativeHandle());
    const bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
    if (focused && !io.WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_F)) FocusSelection();
    const bool wasNavigating = navigating_;
    if (focused && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !ImGui::IsAnyItemActive())
        navigating_ = true;
    if (!focused || !ImGui::IsMouseDown(ImGuiMouseButton_Right)) navigating_ = false;
    if (navigating_ != wasNavigating) {
        glfwSetInputMode(window, GLFW_CURSOR, navigating_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        // Ignore cursor repositioning on the capture frame.
        if (navigating_) return;
    }
    if (!navigating_ || io.KeyCtrl) {
        velocity_ = glm::vec3(0);
        lookVelocity_ = glm::vec2(0);
        return;
    }
    const float dt = std::min(io.DeltaTime, 0.1f);
    if (dt <= 0) return;
    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return float(ImGui::IsKeyDown(positive)) - float(ImGui::IsKeyDown(negative));
    };
    auto& transform = camera_.Get<Iryven::Transform>();
    const glm::vec3 forward = transform.rotation * glm::vec3(0, 0, -1);
    float pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    float yaw = std::atan2(-forward.x, -forward.z);
    const glm::vec2 targetLook = glm::radians(glm::vec2(
        axis(ImGuiKey_UpArrow, ImGuiKey_DownArrow), axis(ImGuiKey_LeftArrow, ImGuiKey_RightArrow)) * lookSpeed_
        - glm::vec2(io.MouseDelta.y, io.MouseDelta.x) * sensitivity_ / dt);
    const float lookBlend = -std::expm1(-lookResponse_ * dt);
    const glm::vec2 delta = targetLook * dt + (lookVelocity_ - targetLook) * (lookBlend / lookResponse_);
    lookVelocity_ += (targetLook - lookVelocity_) * lookBlend;
    const float nextPitch = pitch + delta.x;
    pitch = glm::clamp(nextPitch, glm::radians(-89.0f), glm::radians(89.0f));
    if (pitch != nextPitch) lookVelocity_.x = 0;
    yaw += delta.y;
    transform.rotation = glm::normalize(glm::angleAxis(yaw, glm::vec3(0, 1, 0)) * glm::angleAxis(pitch, glm::vec3(1, 0, 0)));
    glm::vec3 direction(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_S, ImGuiKey_W));
    if (glm::length(direction) > 0) direction = glm::normalize(direction);
    const glm::vec3 targetVelocity = transform.rotation * direction * moveSpeed_ * (io.KeyShift ? 3.0f : 1.0f);
    const float blend = -std::expm1(-moveResponse_ * dt);
    transform.position += targetVelocity * dt + (velocity_ - targetVelocity) * (blend / moveResponse_);
    velocity_ += (targetVelocity - velocity_) * blend;
}
