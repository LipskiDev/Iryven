#include <iryven/iryven.h>

int main() {
    Iryven::Engine engine({
        .title = "Sandbox",
    });

    auto& input = engine.GetInput();

    input.BindAction("Jump", Iryven::Key::Space);
    input.BindAction("Shoot", Iryven::MouseButton::Left);

    input.OnActionPressed("Jump", [] {
        IRYVEN_CLIENT_INFO("Jump");
        });

    input.OnActionPressed("Shoot", [] {
        IRYVEN_CLIENT_INFO("Shoot");
        });

    engine.Run();
}
