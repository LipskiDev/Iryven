#include <iryven/input/input.h>
#include <iryven/input/key.h>
#include <iryven/events/keyboard_event.h>
#include <iryven/events/mouse_event.h>

namespace Iryven {
	void InputHandler::BindAction(std::string name, Key key)
	{
		auto& action = actions_[name];
		action.name = std::move(name);
		action.bindings.emplace_back(key);
	}

	void InputHandler::BindAction(
		std::string name,
		MouseButton button)
	{
		auto& action = actions_[name];
		action.name = std::move(name);
		action.bindings.emplace_back(button);
	}

	bool InputHandler::IsKeyDown(Key key) const
	{
		return IsValidKey(key) && currentKeys_[ToIndex(key)];
	}

	bool InputHandler::WasKeyPressed(Key key) const
	{
		return IsValidKey(key) && pressedKeys_[ToIndex(key)];
	}

	bool InputHandler::WasKeyReleased(Key key) const
	{
		return IsValidKey(key) && releasedKeys_[ToIndex(key)];
	}

	bool InputHandler::IsMouseButtonDown(MouseButton button) const
	{
		return IsValidMouseButton(button) && currentMouseButtons_[ToIndex(button)];
	}

	bool InputHandler::WasMouseButtonPressed(MouseButton button) const
	{
		return IsValidMouseButton(button) && pressedMouseButtons_[ToIndex(button)];
	}

	bool InputHandler::WasMouseButtonReleased(MouseButton button) const
	{
		return IsValidMouseButton(button) && releasedMouseButtons_[ToIndex(button)];
	}

	void InputHandler::OnActionPressed(
		std::string name,
		ActionCallback callback)
	{
		actionPressedCallbacks_[std::move(name)]
			.push_back(std::move(callback));
	}

	void InputHandler::BeginFrame()
	{
		pressedKeys_.reset();
		releasedKeys_.reset();
		pressedMouseButtons_.reset();
		releasedMouseButtons_.reset();
	}

	void InputHandler::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);

		dispatcher.Dispatch<KeyPressedEvent>(
			[this](KeyPressedEvent& event) {
				const Key key = event.GetKeyCode();
				if (!IsValidKey(key)) {
					return false;
				}

				const auto index = ToIndex(key);
				if (!currentKeys_[index]) {
					pressedKeys_.set(index);
				}
				currentKeys_.set(index);
				return false;
			}
		);

		dispatcher.Dispatch<KeyReleasedEvent>(
			[this](KeyReleasedEvent& event) {
				const Key key = event.GetKeyCode();
				if (!IsValidKey(key)) {
					return false;
				}

				const auto index = ToIndex(key);
				if (currentKeys_[index]) {
					releasedKeys_.set(index);
				}
				currentKeys_.reset(index);
				return false;
			}
		);

		dispatcher.Dispatch<MouseButtonPressedEvent>(
			[this](MouseButtonPressedEvent& event) {
				const MouseButton button = event.GetMouseButton();
				if (!IsValidMouseButton(button)) {
					return false;
				}

				const auto index = ToIndex(button);
				if (!currentMouseButtons_[index]) {
					pressedMouseButtons_.set(index);
				}
				currentMouseButtons_.set(index);
				return false;
			}
		);

		dispatcher.Dispatch<MouseButtonReleasedEvent>(
			[this](MouseButtonReleasedEvent& event) {
				const MouseButton button = event.GetMouseButton();
				if (!IsValidMouseButton(button)) {
					return false;
				}

				const auto index = ToIndex(button);
				if (currentMouseButtons_[index]) {
					releasedMouseButtons_.set(index);
				}
				currentMouseButtons_.reset(index);
				return false;
			}
		);
	}

	void InputHandler::EvaluateActions()
	{
		for (auto& [name, action] : actions_) {
			action.down = false;
			action.pressed = false;
			action.released = false;

			for (const InputBinding& binding : action.bindings) {
				std::visit(
					[this, &action](auto input) {
						using T = decltype(input);

						if constexpr (std::same_as<T, Key>) {
							action.down |= IsKeyDown(input);
							action.pressed |= WasKeyPressed(input);
							action.released |= WasKeyReleased(input);
						}
						else if constexpr (
							std::same_as<T, MouseButton>) {
							action.down |= IsMouseButtonDown(input);
							action.pressed |=
								WasMouseButtonPressed(input);
							action.released |=
								WasMouseButtonReleased(input);
						}
					},
					binding
				);
			}

			if (action.pressed) {
				auto callbackIt =
					actionPressedCallbacks_.find(name);

				if (callbackIt != actionPressedCallbacks_.end()) {
					for (const auto& callback : callbackIt->second) {
						callback();
					}
				}
			}
		}
	}
}
