#include <iryven/iryven.h>
#include "development/development_session.h"

struct CubeController {
    float rotationSpeed = 1.5f;
};

int main() {
    Iryven::Development::DevelopmentSession development;

    Iryven::Engine engine({
        .title = "Sandbox",
    });

    Iryven::World& world = engine.GetWorld();
    auto cubeMesh = Iryven::PrimitiveMeshes::Cube();
    auto cylinderMesh = Iryven::PrimitiveMeshes::Cylinder();
    auto material = engine.GetAssets().LoadMaterial("assets/materials/default.material");

    auto camera = world.CreateEntity("Camera");
    camera.Add<Iryven::Transform>(
        glm::vec3{ 0.0f, 0.0f, 5.0f },
        glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f },
        glm::vec3{ 1.0f }
    );
    camera.Add<Iryven::Camera>();

    auto pointLight = world.CreateEntity("Point Light");
    pointLight.Add<Iryven::Transform>(
        glm::vec3{ 1.0f, 0.0f, 0.0f },
        glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f },
        glm::vec3{ 1.0f }
    );
    pointLight.Add<Iryven::Light>(Iryven::Light{
        .type = Iryven::LightType::Point,
        .intensity = 4.0f,
        .range = 5.0f
    });

    auto cube = world.CreateEntity("Cube");
    auto cube2 = world.CreateEntity("Cube2");
    cube.Add<Iryven::Transform>(
        glm::vec3{ 0.0f },
        glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f },
        glm::vec3{ 1.0f }
    );
    cube2.Add<Iryven::Transform>(
        glm::vec3{ 2.0f, 0.0f, 0.0f },
        glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f },
        glm::vec3{ 0.5f }
    );
    cube.Add<CubeController>();
    cube2.Add<CubeController>();
    cube.Add<Iryven::MeshRenderer>(cubeMesh, material);
    cube2.Add<Iryven::MeshRenderer>(cylinderMesh, material);

    auto& input = engine.GetInput();
    world.AddSystem<Iryven::Transform, CubeController>(
        "Rotate cube",
        [&input](float deltaTime, Iryven::Transform& transform,
                 CubeController& controller)
        {
            const float pitchInput =
                static_cast<float>(input.IsKeyDown(Iryven::Key::W)) -
                static_cast<float>(input.IsKeyDown(Iryven::Key::S));
            const float yawInput =
                static_cast<float>(input.IsKeyDown(Iryven::Key::A)) -
                static_cast<float>(input.IsKeyDown(Iryven::Key::D));

            const glm::quat pitch = glm::angleAxis(
                pitchInput * controller.rotationSpeed * deltaTime,
                glm::vec3{ 1.0f, 0.0f, 0.0f });
            const glm::quat yaw = glm::angleAxis(
                yawInput * controller.rotationSpeed * deltaTime,
                glm::vec3{ 0.0f, 1.0f, 0.0f });

            transform.rotation = glm::normalize(
                yaw * pitch * transform.rotation);
        }
    );

    engine.Run();
}
