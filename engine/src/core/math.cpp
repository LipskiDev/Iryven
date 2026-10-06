#include <iryven/core/math.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr float Pi = std::numbers::pi_v<float>;

float ClampTime(float t)
{
	return std::clamp(t, 0.0f, 1.0f);
}

float Mix(float min, float max, float amount)
{
	return min + (max - min) * amount;
}

float OutBounceCurve(float t)
{
	constexpr float n1 = 7.5625f;
	constexpr float d1 = 2.75f;

	if (t < 1.0f / d1) return n1 * t * t;
	if (t < 2.0f / d1) {
		t -= 1.5f / d1;
		return n1 * t * t + 0.75f;
	}
	if (t < 2.5f / d1) {
		t -= 2.25f / d1;
		return n1 * t * t + 0.9375f;
	}
	t -= 2.625f / d1;
	return n1 * t * t + 0.984375f;
}

float OutElasticCurve(float t, float amplitude, float oscillations)
{
	if (t == 0.0f || t == 1.0f) return t;
	amplitude = std::max(amplitude, 0.0f);
	oscillations = std::max(oscillations, 0.0f);
	const float decay = std::pow(2.0f, -10.0f * t);
	const float exponential = 1.0f - decay;
	const float elastic = 1.0f - decay * std::cos(2.0f * Pi * oscillations * t);
	return exponential + (elastic - exponential) * amplitude;
}

} // namespace

namespace Iryven::Tween {

float Linear(float min, float max, float t)
{
	return Mix(min, max, ClampTime(t));
}

float EaseInSine(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - std::cos(t * Pi / 2.0f));
}

float EaseOutSine(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, std::sin(t * Pi / 2.0f));
}

float EaseInOutSine(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, -(std::cos(Pi * t) - 1.0f) / 2.0f);
}

float EaseInQuad(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t * t);
}

float EaseOutQuad(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - (1.0f - t) * (1.0f - t));
}

float EaseInOutQuad(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInCubic(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t * t * t);
}

float EaseOutCubic(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - std::pow(1.0f - t, 3.0f));
}

float EaseInOutCubic(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInQuart(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t * t * t * t);
}

float EaseOutQuart(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - std::pow(1.0f - t, 4.0f));
}

float EaseInOutQuart(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 4.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInQuint(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t * t * t * t * t);
}

float EaseOutQuint(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - std::pow(1.0f - t, 5.0f));
}

float EaseInOutQuint(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInExpo(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f));
}

float EaseOutExpo(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t));
}

float EaseInOutExpo(float min, float max, float t)
{
	t = ClampTime(t);
	float amount;
	if (t == 0.0f || t == 1.0f) amount = t;
	else if (t < 0.5f) amount = std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f;
	else amount = (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInCirc(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - std::sqrt(1.0f - t * t));
}

float EaseOutCirc(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f)));
}

float EaseInOutCirc(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f
		? (1.0f - std::sqrt(1.0f - std::pow(2.0f * t, 2.0f))) / 2.0f
		: (std::sqrt(1.0f - std::pow(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInBack(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;
	return Mix(min, max, c3 * t * t * t - c1 * t * t);
}

float EaseOutBack(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;
	return Mix(min, max, 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f));
}

float EaseInOutBack(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c1 = 1.70158f;
	constexpr float c2 = c1 * 1.525f;
	const float amount = t < 0.5f
		? std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2) / 2.0f
		: (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInBack(float min, float max, float t, float overshoot)
{
	t = ClampTime(t);
	overshoot = std::max(overshoot, 0.0f);
	return Mix(min, max, (overshoot + 1.0f) * t * t * t - overshoot * t * t);
}

float EaseOutBack(float min, float max, float t, float overshoot)
{
	t = ClampTime(t);
	overshoot = std::max(overshoot, 0.0f);
	return Mix(min, max, 1.0f + (overshoot + 1.0f) * std::pow(t - 1.0f, 3.0f)
		+ overshoot * std::pow(t - 1.0f, 2.0f));
}

float EaseInOutBack(float min, float max, float t, float overshoot)
{
	t = ClampTime(t);
	const float scaledOvershoot = std::max(overshoot, 0.0f) * 1.525f;
	const float amount = t < 0.5f
		? std::pow(2.0f * t, 2.0f) * ((scaledOvershoot + 1.0f) * 2.0f * t - scaledOvershoot) / 2.0f
		: (std::pow(2.0f * t - 2.0f, 2.0f)
			* ((scaledOvershoot + 1.0f) * (2.0f * t - 2.0f) + scaledOvershoot) + 2.0f) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInElastic(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c4 = 2.0f * Pi / 3.0f;
	const float amount = t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
		: -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
	return Mix(min, max, amount);
}

float EaseOutElastic(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c4 = 2.0f * Pi / 3.0f;
	const float amount = t == 0.0f ? 0.0f : t == 1.0f ? 1.0f
		: std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
	return Mix(min, max, amount);
}

float EaseInOutElastic(float min, float max, float t)
{
	t = ClampTime(t);
	constexpr float c5 = 2.0f * Pi / 4.5f;
	float amount;
	if (t == 0.0f || t == 1.0f) amount = t;
	else if (t < 0.5f) amount = -(std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)) / 2.0f;
	else amount = std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5) / 2.0f + 1.0f;
	return Mix(min, max, amount);
}

float EaseInElastic(float min, float max, float t, float amplitude, float oscillations)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - OutElasticCurve(1.0f - t, amplitude, oscillations));
}

float EaseOutElastic(float min, float max, float t, float amplitude, float oscillations)
{
	return Mix(min, max, OutElasticCurve(ClampTime(t), amplitude, oscillations));
}

float EaseInOutElastic(float min, float max, float t, float amplitude, float oscillations)
{
	t = ClampTime(t);
	const float amount = t < 0.5f
		? (1.0f - OutElasticCurve(1.0f - 2.0f * t, amplitude, oscillations)) / 2.0f
		: (1.0f + OutElasticCurve(2.0f * t - 1.0f, amplitude, oscillations)) / 2.0f;
	return Mix(min, max, amount);
}

float EaseInBounce(float min, float max, float t)
{
	t = ClampTime(t);
	return Mix(min, max, 1.0f - OutBounceCurve(1.0f - t));
}

float EaseOutBounce(float min, float max, float t)
{
	return Mix(min, max, OutBounceCurve(ClampTime(t)));
}

float EaseInOutBounce(float min, float max, float t)
{
	t = ClampTime(t);
	const float amount = t < 0.5f
		? (1.0f - OutBounceCurve(1.0f - 2.0f * t)) / 2.0f
		: (1.0f + OutBounceCurve(2.0f * t - 1.0f)) / 2.0f;
	return Mix(min, max, amount);
}

} // namespace Iryven::Tween
