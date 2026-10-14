#include "OperationRegistry.hpp"
#include "SingleOperations.hpp"
#include "BatchVideoOperation.hpp"
#include "BatchAudioOperation.hpp"
#include "SettingsOperation.hpp"

namespace mr {
namespace operations {

OperationRegistry::OperationRegistry() {
    registerOperation(std::make_unique<ConvertVideoOperation>());
    registerOperation(std::make_unique<TrimVideoOperation>());
    registerOperation(std::make_unique<ChangeResolutionOperation>());
    registerOperation(std::make_unique<ChangeSpeedOperation>());
    registerOperation(std::make_unique<RotateVideoOperation>());
    registerOperation(std::make_unique<CompressVideoOperation>());
    registerOperation(std::make_unique<AddWatermarkOperation>());
    registerOperation(std::make_unique<AddSubtitlesOperation>());
    registerOperation(std::make_unique<ExtractAudioOperation>());
    registerOperation(std::make_unique<MergeVideoAudioOperation>());
    registerOperation(std::make_unique<StripAudioOperation>());
    registerOperation(std::make_unique<ConcatenateFilesOperation>());
    registerOperation(std::make_unique<CreateGifOperation>());
    registerOperation(std::make_unique<ExtractFramesOperation>());
    registerOperation(std::make_unique<ShowFileInfoOperation>());
    registerOperation(std::make_unique<CompareFilesOperation>());
    registerOperation(std::make_unique<BatchVideoOperation>());
    registerOperation(std::make_unique<BatchAudioOperation>());
    registerOperation(std::make_unique<SettingsOperation>());
}

void OperationRegistry::registerOperation(std::unique_ptr<IOperation> op) {
    if (!op) return;
    std::string id = op->getId();
    m_operations[id] = std::move(op);
    m_registeredOrder.push_back(id);
}

IOperation* OperationRegistry::getOperation(const std::string& id) const {
    auto it = m_operations.find(id);
    if (it != m_operations.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool OperationRegistry::executeOperation(const std::string& id) {
    IOperation* op = getOperation(id);
    if (op) {
        op->execute();
        return true;
    }
    return false;
}

} // namespace operations
} // namespace mr
