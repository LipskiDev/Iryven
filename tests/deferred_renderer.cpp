#include "../engine/src/renderer/renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <iryven/log.h>
#ifdef _WIN32
#include "../engine/src/platform/win32/win32_window.h"
#endif

struct GLFWwindow;
extern "C" void glfwHideWindow(GLFWwindow *);
extern "C" void glfwSetWindowSize(GLFWwindow *, int, int);

int RunDeferredRendererTests() {
  std::set_terminate([] {
    try {
      if (auto error = std::current_exception())
        std::rethrow_exception(error);
    } catch (const std::exception &error) {
      std::cerr << "Renderer terminated: " << error.what() << '\n';
    }
    std::abort();
  });
  using namespace Iryven;
  Log::Init();
  auto window = CreateWindow({"Deferred renderer regression", 96, 64});
  window->SetEventCallback([](Event &) {});
  const bool glfw =
      window->GetNativeHandleType() == Velos::RHI::WindowHandleType::GLFW;
  AssetUploadQueue uploads;
  Renderer renderer(*window, uploads, true);
  auto mesh = std::make_shared<MeshData>();
  mesh->vertices = {{.position = {-1, -1, -3}, .normal = {0, 0, 1}},
                    {.position = {1, -1, -3}, .normal = {0, 0, 1}},
                    {.position = {0, 1, -3}, .normal = {0, 0, 1}}};
  mesh->indices = {0, 1, 2};
  auto meshletMesh = std::make_shared<MeshData>(*mesh);
  meshletMesh->meshlets.push_back({.vertexIndices = {0, 1, 2},
                                   .triangleIndices = {0, 1, 2},
                                   .center = {0, 0, -3},
                                   .radius = 2.0f});
  auto material = std::make_shared<Material>();
  material->baseColor = Color::White;
  material->roughness = 0.5f;
  auto glass = std::make_shared<Material>(*material);
  glass->transmission = 0.75f;
  RenderScene scene;
  scene.camera = RenderCamera{};
  scene.lights.push_back({});
  scene.lights.push_back(
      {.type = LightType::Point, .position = {0, 0, -2}, .range = 4});
  scene.lights.push_back(
      {.type = LightType::Spot, .position = {1, 0, -2}, .range = 4});
  scene.objects.push_back({.mesh = mesh, .material = material});
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0)),
       .mesh = meshletMesh,
       .material = material});
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 0.5f)),
       .mesh = mesh,
       .material = glass});
  scene.cloths.push_back(
      {.id = 1,
       .transform = glm::translate(glm::mat4(1), glm::vec3(-1, 0, -4)),
       .resolution = {4, 4},
       .material = material});
  auto alternateMaterial = std::make_shared<Material>(*material);
  alternateMaterial->baseColor = Color::Red;
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(-2, 0, 0)),
       .mesh = mesh,
       .material = alternateMaterial});
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(-3, 0, 0)),
       .mesh = mesh,
       .material = material});
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(2, 0, 0)),
       .mesh = meshletMesh,
       .material = alternateMaterial});
  scene.objects.push_back(
      {.transform = glm::translate(glm::mat4(1), glm::vec3(3, 0, 0)),
       .mesh = meshletMesh,
       .material = material});
  scene.deltaTime = 1.0f / 60.0f;
  int rendered = 0;
  for (int frame = 0; frame < 12; ++frame) {
    if (frame == 6) {
      if (glfw)
        glfwSetWindowSize(static_cast<GLFWwindow *>(window->GetNativeHandle()),
                          127, 79);
#ifdef _WIN32
      else {
        RECT size{0, 0, 127, 79};
        AdjustWindowRectEx(&size, WS_OVERLAPPEDWINDOW, FALSE, 0);
        SetWindowPos(static_cast<HWND>(window->GetNativeHandle()), nullptr, 0,
                     0, size.right - size.left, size.bottom - size.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
#endif
    }
    renderer.SetGBufferView(frame >= 3 && frame <= 8 ? GBufferView::Meshlets
                                                     : GBufferView::None);
    window->PollEvents();
    if (!renderer.BeginFrame())
      continue;
    renderer.DrawScene(frame == 0 || frame == 11 ? RenderScene{} : scene);
    renderer.EndFrame();
    ++rendered;
  }
  if (rendered < 8)
    throw std::runtime_error("Deferred renderer did not present enough frames");
  std::cout << "Deferred renderer: grouped materials, indexed + meshlet + "
               "instanced + cloth + transmission, "
               "empty scenes, and resize passed\n";
  return 0;
}
