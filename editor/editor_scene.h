#pragma once

#include <filesystem>
#include <array>

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

struct EditorPrimitiveChoice {
    const char* name;
    EditorPrimitiveMesh primitive;
    std::shared_ptr<const Iryven::MeshData> mesh;
};

EditorSceneEntities CreateEditorStarterScene(Iryven::World& world);
void LoadEditorScene(
    Iryven::World& world,
    const std::filesystem::path& path,
    EditorSceneEntities& entities);
const std::array<EditorPrimitiveChoice, 5>& EditorPrimitiveChoices();
void ApplyEditorRenderable(Iryven::Entity entity);
