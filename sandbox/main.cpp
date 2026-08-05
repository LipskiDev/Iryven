#include <iryven/iryven.h>

int main() {
    auto window = Iryven::CreateWindow();

    Iryven::Engine engine({
        .title = "Sandbox",
        .width = 1920,
        .height = 1080
    });

    IRYVEN_CORE_TRACE("TRACE");
    IRYVEN_CORE_INFO("INFO");
    IRYVEN_CORE_WARN("WARN");
    IRYVEN_CORE_ERROR("ERROR");
    IRYVEN_CORE_FATAL("FATAL");


    IRYVEN_CLIENT_TRACE("TRACE");
    IRYVEN_CLIENT_INFO("INFO");
    IRYVEN_CLIENT_WARN("WARN");
    IRYVEN_CLIENT_ERROR("ERROR");
    IRYVEN_CLIENT_FATAL("FATAL");
    [[maybe_unused]] Iryven::World world = engine.CreateWorld();
}
