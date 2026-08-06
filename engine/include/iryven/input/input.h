#pragma once
#include <iryven/input/key.h>
#include <iryven/input/mouse_button.h>
#include <iryven/events/event.h>
#include <iryven/input/input_action.h>

#include <bitset>

namespace Iryven {
	class InputHandler {
	public:
		using ActionCallback = std::function<void()>;

		void BindAction(std::string name, Key key);
		void BindAction(std::string name, MouseButton button);

		[[nodiscard]] bool IsKeyDown(Key key) const;
		[[nodiscard]] bool WasKeyPressed(Key key) const;
		[[nodiscard]] bool WasKeyReleased(Key key) const;

		[[nodiscard]] bool IsMouseButtonDown(MouseButton button) const;
		[[nodiscard]] bool WasMouseButtonPressed(MouseButton button) const;
		[[nodiscard]] bool WasMouseButtonReleased(MouseButton button) const;

		void OnActionPressed(std::string name, ActionCallback callback);

	private:
		friend class Engine;

		void BeginFrame();
		void OnEvent(Event& event);
		void EvaluateActions();

		std::bitset<KeyCount> currentKeys_;
		std::bitset<KeyCount> pressedKeys_;
		std::bitset<KeyCount> releasedKeys_;

		std::bitset<MouseButtonCount> currentMouseButtons_;
		std::bitset<MouseButtonCount> pressedMouseButtons_;
		std::bitset<MouseButtonCount> releasedMouseButtons_;

		std::unordered_map<std::string, InputAction> actions_;

		std::unordered_map<
			std::string,
			std::vector<ActionCallback>
		> actionPressedCallbacks_;
	};
}
