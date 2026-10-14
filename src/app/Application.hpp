#pragma once
#include "AppContext.hpp"
#include "../operations/OperationRegistry.hpp"
#include <memory>

namespace mr {
namespace app {

class Application {
public:
    static Application& instance() {
        static Application s_app;
        return s_app;
    }

    bool initialize();
    int run();
    void shutdown();

    AppContext& getContext() { return m_context; }
    operations::OperationRegistry& getRegistry() { return m_registry; }

private:
    Application();
    ~Application();

    char showMainMenu();

    AppContext& m_context;
    operations::OperationRegistry m_registry;
    bool m_initialized = false;
};

} // namespace app
} // namespace mr
