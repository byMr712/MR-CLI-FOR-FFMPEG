#pragma once
#include "IOperation.hpp"
#include <string>

namespace mr {
namespace operations {

class BatchVideoOperation : public IOperation {
public:
    std::string getId() const override { return "h"; }
    std::string getName() const override;
    void execute() override;
};

} // namespace operations
} // namespace mr
