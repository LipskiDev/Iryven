#include "editor_layer.h"
#include "editor_scene.h"

#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

EditorLayer::EditorLayer(Iryven::Engine& engine) : Layer("Editor"), engine_(engine) {}

void EditorLayer::OnAttach()
{
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FontSizeBase = 16.0f;
    auto& style = ImGui::GetStyle();
    style.WindowPadding = {10, 9};
    style.FramePadding = {7, 4};
    style.ItemSpacing = {7, 6};
    style.ItemInnerSpacing = {5, 4};
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
    RefreshAssets();

    if (std::filesystem::exists(scenePath_)) {
        LoadEditorScene(world, engine_.GetAsyncLoader(), scenePath_, scene);
        camera_ = scene.camera;
        selected_ = scene.cube;
        inspectorAngles_ = selected_.IsAlive() && selected_.Has<Iryven::Transform>()
            ? glm::degrees(glm::eulerAngles(selected_.Get<Iryven::Transform>().rotation))
            : glm::vec3(0.0f);
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
    if (!playing_ && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        deleteSelectedRequested_ = true;
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
        selected_.Add<Iryven::Light>(Iryven::Light{ .type = Iryven::LightType::Point, .intensity = 10.0f, .range = 3.0f });
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

void EditorLayer::DeleteSelectedEntity()
{
    if (!selected_.IsAlive() || (camera_.IsAlive() && selected_.GetId() == camera_.GetId())) return;

    const std::string name = selected_.GetName() ? selected_.GetName() : "Unnamed";
    selected_.Destroy();
    selected_ = {};
    inspectorAngles_ = glm::vec3(0.0f);
    sceneStatus_ = "Deleted " + name;
    saveFailed_ = false;
}

void EditorLayer::DuplicateSelectedEntity()
{
    if (!selected_.IsAlive() || (camera_.IsAlive() && selected_.GetId() == camera_.GetId())) return;

    Iryven::Entity source = selected_;
    auto& world = engine_.GetWorld();
    const std::string sourceName = source.GetName() ? source.GetName() : "Entity";
    const std::string copyBase = sourceName + " Copy";
    std::string copyName = copyBase;
    for (int suffix = 2; world.GetFlecsWorld().lookup(copyName.c_str()).id() != 0; ++suffix)
        copyName = copyBase + " " + std::to_string(suffix);

    Iryven::Entity duplicate = world.CreateEntity(copyName);
    const auto copyComponent = [&]<typename T>() {
        if (source.Has<T>()) duplicate.Add<T>(source.Get<T>());
    };
    copyComponent.template operator()<Iryven::Transform>();
    copyComponent.template operator()<Iryven::Camera>();
    copyComponent.template operator()<Iryven::Light>();
    copyComponent.template operator()<Iryven::RigidBody>();
    copyComponent.template operator()<Iryven::Collider>();
    copyComponent.template operator()<Iryven::UIText>();

    if (source.Has<EditorRenderable>()) {
        duplicate.Add<EditorRenderable>(source.Get<EditorRenderable>());
        ApplyEditorRenderable(duplicate);
    } else if (source.Has<EditorModelAsset>()) {
        duplicate.Add<EditorModelAsset>(source.Get<EditorModelAsset>());
        ApplyEditorModelAsset(duplicate, engine_.GetAsyncLoader());
    } else if (source.Has<Iryven::MeshRenderer>()) {
        duplicate.Add<Iryven::MeshRenderer>(source.Get<Iryven::MeshRenderer>());
    }

    selected_ = duplicate;
    inspectorAngles_ = duplicate.Has<Iryven::Transform>()
        ? glm::degrees(glm::eulerAngles(duplicate.Get<Iryven::Transform>().rotation))
        : glm::vec3(0.0f);
    sceneStatus_ = "Duplicated " + sourceName;
    saveFailed_ = false;
}

void EditorLayer::RenameSelectedEntity()
{
    if (!selected_.IsAlive() || renameBuffer_[0] == '\0') return;
    const auto existing = engine_.GetWorld().GetFlecsWorld().lookup(renameBuffer_.data());
    if (existing.id() != 0 && existing.id() != selected_.GetId()) {
        sceneStatus_ = "An entity named '" + std::string(renameBuffer_.data()) + "' already exists";
        saveFailed_ = true;
        return;
    }
    selected_.SetName(renameBuffer_.data());
    sceneStatus_ = "Renamed entity to " + std::string(renameBuffer_.data());
    saveFailed_ = false;
}

void EditorLayer::RemoveSelectedComponent(PendingComponent component)
{
    if (!selected_.IsAlive()) return;
    switch (component) {
    case PendingComponent::Transform: selected_.Remove<Iryven::Transform>(); break;
    case PendingComponent::Camera: selected_.Remove<Iryven::Camera>(); break;
    case PendingComponent::Light: selected_.Remove<Iryven::Light>(); break;
    case PendingComponent::RigidBody: selected_.Remove<Iryven::RigidBody>(); break;
    case PendingComponent::Collider: selected_.Remove<Iryven::Collider>(); break;
    case PendingComponent::MeshRenderer:
        selected_.Remove<Iryven::MeshRenderer>();
        if (selected_.Has<EditorRenderable>()) selected_.Remove<EditorRenderable>();
        if (selected_.Has<EditorModelAsset>()) selected_.Remove<EditorModelAsset>();
        break;
    case PendingComponent::UIText: selected_.Remove<Iryven::UIText>(); break;
    case PendingComponent::None: return;
    }
    sceneStatus_ = "Removed component";
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
        duplicateSelectedRequested_ = false;
        deleteSelectedRequested_ = false;
        pendingComponentRemoval_ = PendingComponent::None;
        return;
    }

    if (duplicateSelectedRequested_) {
        duplicateSelectedRequested_ = false;
        DuplicateSelectedEntity();
    }

    if (deleteSelectedRequested_) {
        deleteSelectedRequested_ = false;
        DeleteSelectedEntity();
    }

    if (pendingComponentRemoval_ != PendingComponent::None) {
        const auto component = pendingComponentRemoval_;
        pendingComponentRemoval_ = PendingComponent::None;
        RemoveSelectedComponent(component);
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

bool ComponentHeader(const char* label, bool removable, bool& removeRequested)
{
    const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
    if (removable && ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Remove component")) removeRequested = true;
        ImGui::EndPopup();
    }
    return open;
}
}

void EditorLayer::DrawMeshRenderer(Iryven::Entity entity)
{
    if (!entity.Has<EditorRenderable>()) {
        const std::string preview = entity.Has<EditorModelAsset>()
            ? std::filesystem::path{entity.Get<EditorModelAsset>().path}.filename().string()
            : "External model";
        if (ImGui::BeginCombo("Mesh", preview.c_str())) {
            for (const auto& path : projectAssets_) {
                const bool packaged = path.extension() == ".iryasset";
                ImGui::BeginDisabled(!packaged);
                if (ImGui::Selectable(path.filename().string().c_str()) && packaged) AssignModelAsset(path);
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
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
    std::filesystem::path selectedAsset;
    if (ImGui::BeginCombo("##mesh", preview)) {
        for (int index = 0; index < static_cast<int>(choices.size()); ++index) {
            const bool selected = selectedPrimitive == index;
            if (ImGui::Selectable(choices[index].name, selected)) {
                settings.primitive = choices[index].primitive;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        if (!projectAssets_.empty()) ImGui::SeparatorText("Project assets");
        for (const auto& path : projectAssets_) {
            const bool packaged = path.extension() == ".iryasset";
            ImGui::BeginDisabled(!packaged);
            if (ImGui::Selectable(path.filename().string().c_str()) && packaged) selectedAsset = path;
            ImGui::EndDisabled();
            if (!packaged && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Import this source model from the Assets tab first");
        }
        ImGui::EndCombo();
    }
    if (!selectedAsset.empty()) {
        AssignModelAsset(selectedAsset);
        return;
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

void EditorLayer::RefreshAssets()
{
    projectAssets_.clear();
    std::error_code error;
    if (!std::filesystem::exists("assets", error)) return;
    for (std::filesystem::recursive_directory_iterator iterator("assets", error), end;
         iterator != end && !error; iterator.increment(error)) {
        if (!iterator->is_regular_file(error)) continue;
        const auto extension = iterator->path().extension().string();
        if (extension == ".iryasset" || extension == ".obj" || extension == ".gltf" || extension == ".glb")
            projectAssets_.push_back(iterator->path().lexically_normal());
    }
    std::ranges::sort(projectAssets_);
}

void EditorLayer::AssignModelAsset(const std::filesystem::path& path)
{
    if (!selected_.IsAlive() || path.extension() != ".iryasset") return;
    const std::string serializedPath = path.lexically_normal().generic_string();
    if (selected_.Has<EditorModelAsset>()) selected_.Get<EditorModelAsset>().path = serializedPath;
    else selected_.Add<EditorModelAsset>(EditorModelAsset{serializedPath});
    ApplyEditorModelAsset(selected_, engine_.GetAsyncLoader());
    sceneStatus_ = "Loading " + path.generic_string();
    saveFailed_ = false;
}

void EditorLayer::ImportModelAsset(const std::filesystem::path& path)
{
    if (path.extension() == ".iryasset") return;
    auto destination = path;
    destination.replace_extension(".iryasset");
    try {
        engine_.GetAssets().ImportModel(path, destination);
        sceneStatus_ = "Imported " + destination.generic_string();
        saveFailed_ = false;
        RefreshAssets();
    } catch (const std::exception& error) {
        sceneStatus_ = error.what();
        saveFailed_ = true;
    }
}

void EditorLayer::DrawAssetBrowser()
{
    if (ImGui::Button("Refresh", {-1, 0})) RefreshAssets();
    ImGui::Separator();
    if (projectAssets_.empty()) {
        ImGui::TextDisabled("No model assets found");
        ImGui::TextWrapped("Place .obj, .gltf, .glb, or future .iryasset files under assets/.");
        return;
    }
    std::filesystem::path assetToAssign;
    std::filesystem::path sourceToImport;
    for (const auto& path : projectAssets_) {
        const bool packaged = path.extension() == ".iryasset";
        ImGui::PushID(path.generic_string().c_str());
        ImGui::TextDisabled("%s", packaged ? "ASSET" : "SOURCE");
        ImGui::SameLine();
        const bool enabled = !playing_ && (packaged ? selected_.IsAlive() : true);
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(path.filename().string().c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (packaged) assetToAssign = path;
            else sourceToImport = path;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            if (packaged) ImGui::SetTooltip("Double-click to assign to the selected entity");
            else ImGui::SetTooltip("Double-click to import as a self-contained .iryasset");
        }
        ImGui::PopID();
    }
    if (!sourceToImport.empty()) ImportModelAsset(sourceToImport);
    else if (!assetToAssign.empty()) AssignModelAsset(assetToAssign);
}

void EditorLayer::DrawPanels()
{
    const auto size = ImGui::GetIO().DisplaySize;
    const float left = std::min(235.0f, size.x * 0.20f);
    const float right = std::min(325.0f, size.x * 0.28f);
    const float top = 46.0f;
    const float bottom = 28.0f;
    const auto flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({size.x, top});
    ImGui::Begin("##toolbar", nullptr, flags | ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored({0.48f, 0.85f, 0.77f, 1}, "IRYVEN");
    ImGui::SameLine();
    ImGui::TextDisabled("  /  SCENE EDITOR");
    if (size.x > 900) {
        ImGui::SameLine(left + 24);
        ImGui::TextUnformatted("Starter scene");
        ImGui::SameLine();
        ImGui::TextDisabled("  Perspective");
    }
    ImGui::SameLine(size.x - 92.0f);
    if (playing_) ImGui::PushStyleColor(ImGuiCol_Button, {0.52f, 0.16f, 0.18f, 1.0f});
    if (ImGui::Button(playing_ ? "Stop" : "Play", {74, 27})) playToggleRequested_ = true;
    if (playing_) ImGui::PopStyleColor();
    ImGui::End();

    ImGui::SetNextWindowPos({0, top});
    ImGui::SetNextWindowSize({left, std::max(1.0f, size.y - top - bottom)});
    ImGui::Begin("##hierarchy", nullptr, flags);
    if (ImGui::Selectable("Scene", !showAssets_, 0, {70, 24})) showAssets_ = false;
    ImGui::SameLine();
    if (ImGui::Selectable("Assets", showAssets_, 0, {70, 24})) showAssets_ = true;
    ImGui::Separator();
    if (showAssets_) {
        DrawAssetBrowser();
    } else {
    ImGui::BeginDisabled(playing_);
    if (ImGui::Button("+  Create", {-1, 27})) ImGui::OpenPopup("create_entity");
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
            selected_.IsAlive() && selected_.GetId() == entity.GetId(), 0, {0, 23})) {
            selected_ = entity;
            inspectorAngles_ = entity.Has<Iryven::Transform>()
                ? glm::degrees(glm::eulerAngles(entity.Get<Iryven::Transform>().rotation))
                : glm::vec3(0.0f);
        }
        if (ImGui::BeginPopupContextItem("entity_context")) {
            selected_ = entity;
            if (ImGui::MenuItem("Rename", "F2", false, !playing_)) {
                renameBuffer_.fill('\0');
                if (const char* name = entity.GetName()) {
                    const auto length = std::min(std::strlen(name), renameBuffer_.size() - 1);
                    std::copy_n(name, length, renameBuffer_.data());
                }
                renamePopupRequested_ = true;
            }
            const bool isEditorCamera = camera_.IsAlive() && entity.GetId() == camera_.GetId();
            if (ImGui::MenuItem("Duplicate", nullptr, false, !playing_ && !isEditorCamera))
                duplicateSelectedRequested_ = true;
            if (ImGui::MenuItem("Delete", "Del", false, !playing_ && !isEditorCamera))
                deleteSelectedRequested_ = true;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    });
    if (renamePopupRequested_) {
        ImGui::OpenPopup("Rename entity");
        renamePopupRequested_ = false;
    }
    ImGui::SetNextWindowSize({300, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Rename entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetKeyboardFocusHere();
        const bool submitted = ImGui::InputText("Name", renameBuffer_.data(), renameBuffer_.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (submitted || ImGui::Button("Rename")) {
            RenameSelectedEntity();
            if (!saveFailed_) ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    }
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
        if (ImGui::Button("+  Add component", {-1, 27})) ImGui::OpenPopup("add_component");
        DrawAddComponentMenu();
        ImGui::Spacing();
        bool removeComponent = false;
        const bool isEditorCamera = camera_.IsAlive() && selected_.GetId() == camera_.GetId();
        if (selected_.Has<Iryven::Transform>() && ComponentHeader("Transform", !isEditorCamera, removeComponent)) {
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
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::Transform;
        removeComponent = false;
        if (selected_.Has<Iryven::Light>() && ComponentHeader("Light", true, removeComponent)) {
            auto& light = selected_.Get<Iryven::Light>();
            ImGui::Checkbox("Enabled", &light.enabled);
            ImGui::SetNextItemWidth(-1);
            ImGui::ColorEdit3("##lightColor", glm::value_ptr(light.color.value));
            SliderField("Intensity", light.intensity, 0, 100, "%.2f");
            SliderField("Range", light.range, 0, 100, "%.2f");
        }
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::Light;
        removeComponent = false;
        if (selected_.Has<Iryven::Camera>() && ComponentHeader("Camera", !isEditorCamera, removeComponent)) {
            auto& camera = selected_.Get<Iryven::Camera>();
            ImGui::Checkbox("Primary", &camera.primary);
            SliderField("Field of view", camera.verticalFov, 25, 100, "%.0f deg");
        }
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::Camera;
        removeComponent = false;
        if (selected_.Has<Iryven::RigidBody>() && ComponentHeader("Rigid Body", true, removeComponent)) {
            auto& body = selected_.Get<Iryven::RigidBody>();
            const char* bodyTypes[] = {"Static", "Kinematic", "Dynamic"};
            int bodyType = static_cast<int>(body.type);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##bodyType", &bodyType, bodyTypes, IM_ARRAYSIZE(bodyTypes)))
                body.type = static_cast<Iryven::BodyType>(bodyType);
            SliderField("Gravity scale", body.gravityScale, 0, 5, "%.2f");
            ImGui::Checkbox("Fixed rotation", &body.fixedRotation);
        }
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::RigidBody;
        removeComponent = false;
        if (selected_.Has<Iryven::Collider>() && ComponentHeader("Collider", true, removeComponent)) {
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
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::Collider;
        removeComponent = false;
        if (selected_.Has<Iryven::MeshRenderer>() && ComponentHeader("Mesh Renderer", true, removeComponent)) {
            DrawMeshRenderer(selected_);
        }
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::MeshRenderer;
        removeComponent = false;
        if (selected_.Has<Iryven::UIText>() && ComponentHeader("UI Text", true, removeComponent)) {
            auto& text = selected_.Get<Iryven::UIText>();
            SliderField("Font size", text.fontSize, 6, 144, "%.0f px");
            ImGui::TextDisabled("Assign a font and text from code");
        }
        if (removeComponent) pendingComponentRemoval_ = PendingComponent::UIText;
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
	if (ImGui::CollapsingHeader("Lighting debug")) {
		if (ImGui::Checkbox("Light count", &lightCountView_)) {
			if (lightCountView_) shadowTierView_ = false;
			engine_.SetLightCountView(lightCountView_);
			engine_.SetShadowTierView(shadowTierView_);
		}
		if (ImGui::Checkbox("Shadow resolution", &shadowTierView_)) {
			if (shadowTierView_) lightCountView_ = false;
			engine_.SetLightCountView(lightCountView_);
			engine_.SetShadowTierView(shadowTierView_);
		}
		if (shadowTierView_) {
			ImGui::TextColored({1.0f, 0.2f, 0.15f, 1.0f}, "High / 1024");
			ImGui::TextColored({1.0f, 0.8f, 0.1f, 1.0f}, "Medium / 512");
			ImGui::TextColored({0.15f, 0.45f, 1.0f, 1.0f}, "Low / 256");
			ImGui::TextColored({0.65f, 0.25f, 1.0f, 1.0f}, "Very low / 128");
			ImGui::TextDisabled("Black = no shadowed point light");
		}
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
