#pragma once

#include <memory>

namespace Velos::RHI {
class IDevice;
}

namespace Iryven {
// Private GPU backend. Neither editor layers nor public render APIs see it.
class ImGuiRenderer final {
 public:
  explicit ImGuiRenderer(Velos::RHI::IDevice &device);
  ~ImGuiRenderer();
  ImGuiRenderer(const ImGuiRenderer &) = delete;
  ImGuiRenderer &operator=(const ImGuiRenderer &) = delete;
  void BeginFrame();
  void Draw();

 private:
  struct D3D12State;
  std::unique_ptr<D3D12State> d3d12_;
  Velos::RHI::IDevice &device_;
};
}  // namespace Iryven
