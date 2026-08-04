#include <iryven/iryven.h>

int main() {
    auto window = Iryven::CreateWindow();

    Iryven::Engine engine({
        .title = "Sandbox",
        .width = 1600,
        .height = 900
    });

    [[maybe_unused]] Iryven::World world = engine.CreateWorld();
}
