#pragma once

#include "event.h"
#include <iryven/input/mouse_button.h>
#include <sstream>

namespace Iryven {
	class MouseMovedEvent : public Event {
	public:
		MouseMovedEvent(float x, float y) :mouseX_(x), mouseY_(y) {}

		inline float GetX() const { return mouseX_; }
		inline float GetY() const { return mouseY_;  }

		std::string ToString() const override {
			std::stringstream ss;
			ss << "MouseMovedEvent: " << mouseX_ << ", " << mouseY_;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseMoved);
		EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input);

	private:
		float mouseX_, mouseY_;
	};

	class MouseScrolledEvent : public Event {
	public:
		MouseScrolledEvent(float xOffset, float yOffset) :xOffset_(xOffset), yOffset_(yOffset) {}

		inline float GetX() const { return xOffset_; }
		inline float GetY() const { return yOffset_; }

		std::string ToString() const override {
			std::stringstream ss;
			ss << "MouseScrolledEvent: " << xOffset_ << ", " << yOffset_;
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseScrolled)
		EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input);

	private:
		float xOffset_, yOffset_;
	};

	class MouseButtonEvent : public Event {
	public:
		inline MouseButton GetMouseButton() const { return button_; }

		EVENT_CLASS_CATEGORY(EventCategory::Mouse | EventCategory::Input | EventCategory::MouseButtonCategory)

	protected:
		MouseButtonEvent(MouseButton button) : button_(button) {}

		MouseButton button_;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent {
	public:
		MouseButtonPressedEvent(MouseButton button) : MouseButtonEvent(button) {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "MouseButtonPressedEvent: " << Iryven::ToString(button_);
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent {
	public:
		MouseButtonReleasedEvent(MouseButton button) : MouseButtonEvent(button) {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "MouseButtonReleasedEvent: " << Iryven::ToString(button_);
			return ss.str();
		}

		EVENT_CLASS_TYPE(MouseButtonReleased)
	};
}
