#pragma once

#include <memory>

namespace Iryven::Development {

class DevelopmentSession {
public:
    DevelopmentSession();
    ~DevelopmentSession();

    DevelopmentSession(const DevelopmentSession&) = delete;
    DevelopmentSession& operator=(const DevelopmentSession&) = delete;
    DevelopmentSession(DevelopmentSession&&) = delete;
    DevelopmentSession& operator=(DevelopmentSession&&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Iryven::Development
