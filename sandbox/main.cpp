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

    auto camera = world.CreateEntity("Camera");
    camera.Add<Iryven::Transform>(
        glm::vec3{ 0.0f, 0.0f, 5.0f },
        glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f },
        glm::vec3{ 1.0f }
    );
    camera.Add<Iryven::Camera>();

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
    cube.Add<Iryven::MeshRenderer>(
        std::vector<Iryven::Vertex>{
            {{-0.75f, -0.75f, -0.75f}, {1.0f, 0.2f, 0.2f}},
            {{ 0.75f, -0.75f, -0.75f}, {0.2f, 1.0f, 0.2f}},
            {{ 0.75f,  0.75f, -0.75f}, {0.2f, 0.4f, 1.0f}},
            {{-0.75f,  0.75f, -0.75f}, {1.0f, 0.8f, 0.2f}},
            {{-0.75f, -0.75f,  0.75f}, {0.8f, 0.2f, 1.0f}},
            {{ 0.75f, -0.75f,  0.75f}, {0.2f, 1.0f, 1.0f}},
            {{ 0.75f,  0.75f,  0.75f}, {1.0f, 0.4f, 0.7f}},
            {{-0.75f,  0.75f,  0.75f}, {0.6f, 1.0f, 0.3f}}
        },
        std::vector<std::uint32_t>{
            4, 5, 6, 4, 6, 7,
            1, 0, 3, 1, 3, 2,
            0, 4, 7, 0, 7, 3,
            5, 1, 2, 5, 2, 6,
            3, 7, 6, 3, 6, 2,
            0, 1, 5, 0, 5, 4
        }
    );

    cube2.Add<Iryven::MeshRenderer>(
        std::vector<Iryven::Vertex>{
            {{-0.75f, -0.75f, -0.75f}, { 1.0f, 0.2f, 0.2f }},
            { { 0.75f, -0.75f, -0.75f}, {0.2f, 1.0f, 0.2f} },
            { { 0.75f,  0.75f, -0.75f}, {0.2f, 0.4f, 1.0f} },
            { {-0.75f,  0.75f, -0.75f}, {1.0f, 0.8f, 0.2f} },
            { {-0.75f, -0.75f,  0.75f}, {0.8f, 0.2f, 1.0f} },
            { { 0.75f, -0.75f,  0.75f}, {0.2f, 1.0f, 1.0f} },
            { { 0.75f,  0.75f,  0.75f}, {1.0f, 0.4f, 0.7f} },
            { {-0.75f,  0.75f,  0.75f}, {0.6f, 1.0f, 0.3f} }
    },
        std::vector<std::uint32_t>{
        4, 5, 6, 4, 6, 7,
            1, 0, 3, 1, 3, 2,
            0, 4, 7, 0, 7, 3,
            5, 1, 2, 5, 2, 6,
            3, 7, 6, 3, 6, 2,
            0, 1, 5, 0, 5, 4
    }
    );

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
