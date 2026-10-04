#pragma once

#include <filesystem>
#include <array>
#include <string>

#include <iryven/iryven.h>

struct EditorSceneEntities {
    Iryven::Entity camera;
    Iryven::Entity cube;
    Iryven::Entity floor;
    Iryven::Entity light;
};

enum class EditorPrimitiveMesh {
    Cube,
    Sphere,
    Cylinder,
    Cone,
    Plane,
};

struct EditorRenderable {
    EditorPrimitiveMesh primitive = EditorPrimitiveMesh::Cube;
    Iryven::Color baseColor = Iryven::Color::White;
    float roughness = 0.0f;
    float metallic = 0.0f;
    Iryven::Color emissive = Iryven::Color::Black;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
};

// Serializable recipe for a runtime MeshRenderer backed by a packaged model.
// Runtime asset handles are process-local, so scenes persist the project path
// and request a fresh handle when they are loaded.
struct EditorModelAsset {
    std::string path;
};

struct EditorPrimitiveChoice {
    const char* name;
    EditorPrimitiveMesh primitive;
    std::shared_ptr<const Iryven::MeshData> mesh;
};

EditorSceneEntities CreateEditorStarterScene(Iryven::World& world);
void LoadEditorScene(
    Iryven::World& world,
    Iryven::AsynchronousLoader& loader,
    const std::filesystem::path& path,
    EditorSceneEntities& entities);
const std::array<EditorPrimitiveChoice, 5>& EditorPrimitiveChoices();
void ApplyEditorRenderable(Iryven::Entity entity);
void ApplyEditorModelAsset(Iryven::Entity entity, Iryven::AsynchronousLoader& loader);
