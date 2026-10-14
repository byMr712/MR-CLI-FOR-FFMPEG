#pragma once
#include "IOperation.hpp"
#include <string>

namespace mr {
namespace operations {

class BatchAudioOperation : public IOperation {
public:
    std::string getId() const override { return "i"; }
    std::string getName() const override;
    void execute() override;
};

} // namespace operations
} // namespace mr
