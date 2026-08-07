#include "development_session.h"

#include <stdexcept>

#ifdef IRYVEN_WITH_LIVEPP
#include <LivePP/API/x64/LPP_API_x64_CPP.h>
#endif

namespace Iryven::Development {

struct DevelopmentSession::Impl {
#ifdef IRYVEN_WITH_LIVEPP
    Impl()
        : livePPAgent(lpp::LppCreateDefaultAgent(nullptr, L"external/LivePP")) {
        if (!lpp::LppIsValidDefaultAgent(&livePPAgent)) {
            throw std::runtime_error("Failed to initialize Live++");
        }

        livePPAgent.EnableModule(
            lpp::LppGetCurrentModulePath(),
            lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES,
            nullptr,
            nullptr);
    }

    ~Impl() {
        lpp::LppDestroyDefaultAgent(&livePPAgent);
    }

    lpp::LppDefaultAgent livePPAgent;
#endif
};

DevelopmentSession::DevelopmentSession()
    : impl_(std::make_unique<Impl>()) {}

DevelopmentSession::~DevelopmentSession() = default;

} // namespace Iryven::Development
