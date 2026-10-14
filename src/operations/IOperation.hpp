#pragma once
#include <string>

namespace mr {
namespace operations {

class IOperation {
public:
    virtual ~IOperation() = default;
    virtual std::string getId() const = 0;
    virtual std::string getName() const = 0;
    virtual void execute() = 0;
};

} // namespace operations
} // namespace mr
