#include "editor_layer.h"

#include <exception>
#include <iostream>

int main()
{
    try {
        Iryven::Engine engine({ .title = "Iryven Editor", .width = 1600, .height = 900, .enableImGui = true });
        engine.PushOverlay(std::make_unique<EditorLayer>(engine));
        engine.Run();
    } catch (const std::exception& error) {
        std::cerr << "Editor: " << error.what() << '\n';
        return 1;
    }
}
