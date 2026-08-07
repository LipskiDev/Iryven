#include <iryven/iryven.h>

#include <cassert>

int main() {
    Iryven::Engine engine({ .title = "Test"});
    assert(engine.GetConfig().title == "Test");
    [[maybe_unused]] auto& world = engine.GetWorld();
}
