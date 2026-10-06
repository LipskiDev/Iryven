#pragma once

namespace Iryven::Tween {

// Interpolate from min to max using normalized time t. Time is clamped to [0, 1].
[[nodiscard]] float Linear(float min, float max, float t);

[[nodiscard]] float EaseInSine(float min, float max, float t);
[[nodiscard]] float EaseOutSine(float min, float max, float t);
[[nodiscard]] float EaseInOutSine(float min, float max, float t);

[[nodiscard]] float EaseInQuad(float min, float max, float t);
[[nodiscard]] float EaseOutQuad(float min, float max, float t);
[[nodiscard]] float EaseInOutQuad(float min, float max, float t);

[[nodiscard]] float EaseInCubic(float min, float max, float t);
[[nodiscard]] float EaseOutCubic(float min, float max, float t);
[[nodiscard]] float EaseInOutCubic(float min, float max, float t);

[[nodiscard]] float EaseInQuart(float min, float max, float t);
[[nodiscard]] float EaseOutQuart(float min, float max, float t);
[[nodiscard]] float EaseInOutQuart(float min, float max, float t);

[[nodiscard]] float EaseInQuint(float min, float max, float t);
[[nodiscard]] float EaseOutQuint(float min, float max, float t);
[[nodiscard]] float EaseInOutQuint(float min, float max, float t);

[[nodiscard]] float EaseInExpo(float min, float max, float t);
[[nodiscard]] float EaseOutExpo(float min, float max, float t);
[[nodiscard]] float EaseInOutExpo(float min, float max, float t);

[[nodiscard]] float EaseInCirc(float min, float max, float t);
[[nodiscard]] float EaseOutCirc(float min, float max, float t);
[[nodiscard]] float EaseInOutCirc(float min, float max, float t);

[[nodiscard]] float EaseInBack(float min, float max, float t);
[[nodiscard]] float EaseOutBack(float min, float max, float t);
[[nodiscard]] float EaseInOutBack(float min, float max, float t);
// overshoot controls how far the curve travels beyond its endpoint.
[[nodiscard]] float EaseInBack(float min, float max, float t, float overshoot);
[[nodiscard]] float EaseOutBack(float min, float max, float t, float overshoot);
[[nodiscard]] float EaseInOutBack(float min, float max, float t, float overshoot);

[[nodiscard]] float EaseInElastic(float min, float max, float t);
[[nodiscard]] float EaseOutElastic(float min, float max, float t);
[[nodiscard]] float EaseInOutElastic(float min, float max, float t);
// amplitude controls the overshoot strength; oscillations controls the wave count.
[[nodiscard]] float EaseInElastic(float min, float max, float t, float amplitude, float oscillations);
[[nodiscard]] float EaseOutElastic(float min, float max, float t, float amplitude, float oscillations);
[[nodiscard]] float EaseInOutElastic(float min, float max, float t, float amplitude, float oscillations);

[[nodiscard]] float EaseInBounce(float min, float max, float t);
[[nodiscard]] float EaseOutBounce(float min, float max, float t);
[[nodiscard]] float EaseInOutBounce(float min, float max, float t);

} // namespace Iryven::Tween
