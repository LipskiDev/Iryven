#include "editor_scene.h"

#include <algorithm>
#include <cstddef>

namespace {

void RegisterEditorSceneComponents(Iryven::World& world)
{
    auto& ecs = world.GetFlecsWorld();
    ecs.component<std::string>().opaque(flecs::String)
        .serialize([](const flecs::serializer* serializer, const std::string* value) {
            const char* text = value->c_str();
            return serializer->value(flecs::String, &text);
        })
        .assign_string([](std::string* value, const char* text) {
            *value = text ? text : "";
        });
    ecs.component<EditorModelAsset>()
        .member<std::string>("path", 0, offsetof(EditorModelAsset, path));
    ecs.component<EditorRenderable>()
        .member<EditorPrimitiveMesh>("primitive", 0, offsetof(EditorRenderable, primitive))
        .member<Iryven::Color>("baseColor", 0, offsetof(EditorRenderable, baseColor))
        .member<float>("roughness", 0, offsetof(EditorRenderable, roughness))
        .member<float>("metallic", 0, offsetof(EditorRenderable, metallic))
        .member<Iryven::Color>("emissive", 0, offsetof(EditorRenderable, emissive))
        .member<float>("normalScale", 0, offsetof(EditorRenderable, normalScale))
        .member<float>("occlusionStrength", 0, offsetof(EditorRenderable, occlusionStrength));
}

void EnsureEditorRenderable(Iryven::Entity entity, const EditorRenderable& fallback)
{
    // A starter entity may have been changed from a primitive to a packaged
    // model. Its serialized model recipe takes precedence over the fallback.
    if (entity.Has<EditorModelAsset>()) return;
    if (!entity.Has<EditorRenderable>()) entity.Add<EditorRenderable>(fallback);
    ApplyEditorRenderable(entity);
}

} // namespace

const std::array<EditorPrimitiveChoice, 5>& EditorPrimitiveChoices()
{
    static const std::array<EditorPrimitiveChoice, 5> choices{{
        {"Cube", EditorPrimitiveMesh::Cube, Iryven::PrimitiveMeshes::Cube()},
        {"Sphere", EditorPrimitiveMesh::Sphere, Iryven::PrimitiveMeshes::Sphere()},
        {"Cylinder", EditorPrimitiveMesh::Cylinder, Iryven::PrimitiveMeshes::Cylinder()},
        {"Cone", EditorPrimitiveMesh::Cone, Iryven::PrimitiveMeshes::Cone()},
        {"Plane", EditorPrimitiveMesh::Plane, Iryven::PrimitiveMeshes::Plane()},
    }};
    return choices;
}

void ApplyEditorRenderable(Iryven::Entity entity)
{
    if (!entity.Has<EditorRenderable>()) return;
    const auto& settings = entity.Get<EditorRenderable>();
    const auto& choices = EditorPrimitiveChoices();
    const auto choice = std::find_if(choices.begin(), choices.end(), [&](const EditorPrimitiveChoice& candidate) {
        return candidate.primitive == settings.primitive;
    });
    if (choice == choices.end()) return;
    if (entity.Has<EditorModelAsset>()) entity.Remove<EditorModelAsset>();

    auto material = std::make_shared<Iryven::Material>();
    material->baseColor = settings.baseColor;
    material->roughness = settings.roughness;
    material->metallic = settings.metallic;
    material->emissive = settings.emissive;
    material->normalScale = settings.normalScale;
    material->occlusionStrength = settings.occlusionStrength;

    if (!entity.Has<Iryven::MeshRenderer>()) {
        entity.Add<Iryven::MeshRenderer>(choice->mesh, std::move(material));
        return;
    }
    auto& renderer = entity.Get<Iryven::MeshRenderer>();
    renderer.mesh = choice->mesh;
    renderer.model.reset();
    renderer.modelAsset = {};
    renderer.material = std::move(material);
}

void ApplyEditorModelAsset(Iryven::Entity entity, Iryven::AsynchronousLoader& loader)
{
    if (!entity.Has<EditorModelAsset>()) return;
    const auto path = std::filesystem::path{entity.Get<EditorModelAsset>().path};
    if (path.empty()) return;

    const auto handle = loader.RequestModel(path);
    if (entity.Has<EditorRenderable>()) entity.Remove<EditorRenderable>();
    if (!entity.Has<Iryven::MeshRenderer>()) {
        entity.Add<Iryven::MeshRenderer>(handle);
        return;
    }
    auto& renderer = entity.Get<Iryven::MeshRenderer>();
    renderer.mesh.reset();
    renderer.model.reset();
    renderer.modelAsset = handle;
    renderer.material.reset();
}

EditorSceneEntities CreateEditorStarterScene(Iryven::World& world)
{
    RegisterEditorSceneComponents(world);
    EditorSceneEntities entities;
    entities.camera = world.CreateEntity("Editor Camera");
    entities.camera.Add<Iryven::Transform>(Iryven::Transform{
        .position = {0.0f, 2.0f, 7.0f},
        .rotation = glm::angleAxis(glm::radians(-16.0f), glm::vec3(1, 0, 0)) });
    entities.camera.Add<Iryven::Camera>();

    return entities;
}

void LoadEditorScene(
    Iryven::World& world,
    Iryven::AsynchronousLoader& loader,
    const std::filesystem::path& path,
    EditorSceneEntities& entities)
{
    if (!std::filesystem::exists(path)) return;
    world.DeserializeScene(path);

    entities.camera = world.CreateEntity("Editor Camera");
    entities.cube = world.CreateEntity("Cube");
    entities.floor = world.CreateEntity("Floor");
    entities.light = world.CreateEntity("Sun");

    EnsureEditorRenderable(entities.cube, EditorRenderable{
        .baseColor = Iryven::Color{0.8f, 0.45f, 0.2f}, .roughness = 0.7f });
    EnsureEditorRenderable(entities.floor, EditorRenderable{
        .baseColor = Iryven::Color{0.35f, 0.38f, 0.42f}, .roughness = 1.0f });
    world.ForEachEntity([](Iryven::Entity entity) {
        if (entity.Has<EditorRenderable>()) ApplyEditorRenderable(entity);
    });
    world.ForEachEntity([&loader](Iryven::Entity entity) {
        if (entity.Has<EditorModelAsset>()) ApplyEditorModelAsset(entity, loader);
    });
}
