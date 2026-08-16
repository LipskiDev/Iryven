#pragma once

#include <iryven/rendering/render_scene.h>

namespace Iryven {

// Public rendering surface available to layers. Frame acquisition and
// presentation remain owned by Engine and are intentionally not exposed here.
class RenderContext {
public:
    virtual ~RenderContext() = default;
    virtual void DrawScene(const RenderScene& renderScene) = 0;
};

} // namespace Iryven
