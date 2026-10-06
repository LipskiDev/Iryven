#include <iryven/core/math.h>

#include <array>
#include <cassert>
#include <cmath>

void RunTweenTests()
{
	using Easing = float (*)(float, float, float);
	constexpr std::array<Easing, 31> easings{
		Iryven::Tween::Linear,
		Iryven::Tween::EaseInSine, Iryven::Tween::EaseOutSine, Iryven::Tween::EaseInOutSine,
		Iryven::Tween::EaseInQuad, Iryven::Tween::EaseOutQuad, Iryven::Tween::EaseInOutQuad,
		Iryven::Tween::EaseInCubic, Iryven::Tween::EaseOutCubic, Iryven::Tween::EaseInOutCubic,
		Iryven::Tween::EaseInQuart, Iryven::Tween::EaseOutQuart, Iryven::Tween::EaseInOutQuart,
		Iryven::Tween::EaseInQuint, Iryven::Tween::EaseOutQuint, Iryven::Tween::EaseInOutQuint,
		Iryven::Tween::EaseInExpo, Iryven::Tween::EaseOutExpo, Iryven::Tween::EaseInOutExpo,
		Iryven::Tween::EaseInCirc, Iryven::Tween::EaseOutCirc, Iryven::Tween::EaseInOutCirc,
		static_cast<Easing>(Iryven::Tween::EaseInBack), static_cast<Easing>(Iryven::Tween::EaseOutBack),
		static_cast<Easing>(Iryven::Tween::EaseInOutBack),
		static_cast<Easing>(Iryven::Tween::EaseInElastic), static_cast<Easing>(Iryven::Tween::EaseOutElastic),
		static_cast<Easing>(Iryven::Tween::EaseInOutElastic),
		Iryven::Tween::EaseInBounce, Iryven::Tween::EaseOutBounce, Iryven::Tween::EaseInOutBounce,
	};

	for (const Easing easing : easings) {
		assert(std::abs(easing(-4.0f, 6.0f, 0.0f) + 4.0f) < 0.0001f);
		assert(std::abs(easing(-4.0f, 6.0f, 1.0f) - 6.0f) < 0.0001f);
		assert(easing(-4.0f, 6.0f, -1.0f) == easing(-4.0f, 6.0f, 0.0f));
		assert(easing(-4.0f, 6.0f, 2.0f) == easing(-4.0f, 6.0f, 1.0f));
	}

	assert(Iryven::Tween::Linear(10.0f, 20.0f, 0.5f) == 15.0f);
	assert(std::abs(Iryven::Tween::EaseInOutSine(10.0f, 20.0f, 0.5f) - 15.0f) < 0.0001f);
	assert(Iryven::Tween::EaseOutBack(0.0f, 1.0f, 0.8f, 3.0f)
		> Iryven::Tween::EaseOutBack(0.0f, 1.0f, 0.8f, 1.0f));
	assert(Iryven::Tween::EaseOutElastic(2.0f, 7.0f, 0.0f, 1.5f, 4.0f) == 2.0f);
	assert(Iryven::Tween::EaseOutElastic(2.0f, 7.0f, 1.0f, 1.5f, 4.0f) == 7.0f);
	assert(Iryven::Tween::EaseInOutElastic(2.0f, 7.0f, -1.0f, 1.5f, 4.0f) == 2.0f);
	assert(Iryven::Tween::EaseInOutElastic(2.0f, 7.0f, 2.0f, 1.5f, 4.0f) == 7.0f);
}
