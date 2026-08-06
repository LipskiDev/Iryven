#pragma once

#include <iryven/events/event.h>

#include <sstream>

namespace Iryven {
	class WindowResizeEvent : public Event {
	public:
		WindowResizeEvent(uint32_t width, uint32_t height) : width_(width), height_(height) {}

		inline uint32_t GetWidth() const { return width_; }
		inline uint32_t GetHeight() const { return height_; }

		std::string ToString() const override {
			std::stringstream ss;
			ss << "WindowResizeEvent: " << width_ << ", " << height_;
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowResize)
		EVENT_CLASS_CATEGORY(EventCategory::Application)

	private:
		uint32_t width_;
		uint32_t height_;
	};

	class WindowCloseEvent : public Event {
	public:
		WindowCloseEvent() {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "WindowCloseEvent";
			return ss.str();
		}

		EVENT_CLASS_TYPE(WindowClose)
		EVENT_CLASS_CATEGORY(EventCategory::Application)
	};

	class AppTickEvent : public Event {
	public:
		AppTickEvent() {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "AppTickEvent";
			return ss.str();
		}

		EVENT_CLASS_TYPE(AppTick)
		EVENT_CLASS_CATEGORY(EventCategory::Application)
	};

	class AppUpdateEvent : public Event {
	public:
		AppUpdateEvent() {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "AppUpdateEvent";
			return ss.str();
		}

		EVENT_CLASS_TYPE(AppUpdate)
		EVENT_CLASS_CATEGORY(EventCategory::Application)
	};

	class AppRenderEvent : public Event {
	public:
		AppRenderEvent() {}

		std::string ToString() const override {
			std::stringstream ss;
			ss << "AppRenderEvent";
			return ss.str();
		}

		EVENT_CLASS_TYPE(AppRender)
		EVENT_CLASS_CATEGORY(EventCategory::Application)
	};
}
