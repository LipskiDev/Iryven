#pragma once
#include <functional>
#include <ostream>
#include <string>

namespace Iryven {
	enum class EventType {
		None = 0,
		WindowClose, WindowResize, WindowFocus, WindowLostFocus, WindowMoved,
		AppTick, AppUpdate, AppRender,
		KeyPressed, KeyReleased,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseScrolled,
	};

	enum EventCategory {
		None = 0,
		Application = 1 << 0,
		Input = 1 << 1,
		Keyboard = 1 << 2,
		Mouse = 1 << 3,
		MouseButtonCategory = 1 << 4,
	};

	#define EVENT_CLASS_TYPE(type) static EventType GetStaticType() { return EventType::##type; }\
								virtual EventType GetEventType() const override { return GetStaticType(); }\
								virtual const char* GetName() const override { return #type; }

	#define EVENT_CLASS_CATEGORY(category) virtual int GetCategoryFlags() const override { return category; }

	class Event {
		friend class EventDispatcher;
	public:
		virtual EventType GetEventType() const = 0;
		virtual const char* GetName() const = 0;
		virtual int GetCategoryFlags() const = 0;
		virtual std::string ToString() const { return GetName(); }

		inline bool IsInCategory(EventCategory category) {
			return GetCategoryFlags() & (int)category;
		}

		[[nodiscard]] bool IsHandled() const noexcept { return isHandled_; }
		void SetHandled(bool handled = true) noexcept { isHandled_ = handled; }

	protected:
		bool isHandled_ = false;
	};

	class EventDispatcher {
		template<typename T>
		using EventFn = std::function<bool(T&)>;
	public:
		EventDispatcher(Event& event) :event_(event) {}

		template<typename T>
		bool Dispatch(EventFn<T> func)
		{
			if (event_.GetEventType() == T::GetStaticType()) {
				event_.isHandled_ = func(*(T*)&event_);
				return true;
			}

			return false;
		}

	private:
		Event& event_;
	};

	inline std::ostream& operator<<(std::ostream& os, const Event& e)
	{
		return os << e.ToString();
	}

}
