#pragma once

#include "event.h"
#include <iryven/input/key.h>

#include <sstream>

namespace Iryven {
	class KeyEvent : public Event
	{
	public:
		inline Key GetKeyCode() const { return keyCode_; }

		EVENT_CLASS_CATEGORY(Keyboard | Input)
	protected:
		KeyEvent(Key keycode)
			: keyCode_(keycode) {
		}

		Key keyCode_;
	};

	class KeyPressedEvent : public KeyEvent
	{
	public:
		KeyPressedEvent(Key keycode, int repeatCount)
			: KeyEvent(keycode), repeatCount_(repeatCount) {
		}

		inline int GetRepeatCount() const { return repeatCount_; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyPressedEvent: " << Iryven::ToString(keyCode_) << " (" << repeatCount_ << " repeats)";
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyPressed)
	private:
		int repeatCount_;
	};

	class KeyReleasedEvent : public KeyEvent
	{
	public:
		KeyReleasedEvent(Key keycode)
			: KeyEvent(keycode) {
		}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "KeyReleasedEvent: " << Iryven::ToString(keyCode_);
			return ss.str();
		}

		EVENT_CLASS_TYPE(KeyReleased)
	};
}
