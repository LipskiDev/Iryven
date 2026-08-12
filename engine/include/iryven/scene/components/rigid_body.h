#pragma once

namespace Iryven {

enum class BodyType {
    Static,
    Kinematic,
    Dynamic
};

struct RigidBody {
    BodyType type = BodyType::Static;
    float gravityScale = 1.0f;
    bool fixedRotation = false;
};

} // namespace Iryven
