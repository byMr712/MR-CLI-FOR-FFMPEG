#pragma once
#include "IOperation.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mr {
namespace operations {

class OperationRegistry {
public:
    OperationRegistry();
    ~OperationRegistry() = default;

    void registerOperation(std::unique_ptr<IOperation> op);
    IOperation* getOperation(const std::string& id) const;
    bool executeOperation(const std::string& id);

    const std::vector<std::string>& getRegisteredIds() const { return m_registeredOrder; }

private:
    std::map<std::string, std::unique_ptr<IOperation>> m_operations;
    std::vector<std::string> m_registeredOrder;
};

} // namespace operations
} // namespace mr
