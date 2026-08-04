#include <iryven/iryven.h>

#include <cassert>

int main() {
    Iryven::Engine engine({ .title = "Test", .width = 1, .height = 1 });
    assert(engine.GetConfig().title == "Test");
    [[maybe_unused]] auto world = engine.CreateWorld();
}
