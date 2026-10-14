#pragma once
#include "IOperation.hpp"
#include <string>
#include <memory>

namespace mr {
namespace operations {

class ConvertVideoOperation : public IOperation {
public:
    std::string getId() const override { return "1"; }
    std::string getName() const override;
    void execute() override;
};

class TrimVideoOperation : public IOperation {
public:
    std::string getId() const override { return "2"; }
    std::string getName() const override;
    void execute() override;
};

class ChangeResolutionOperation : public IOperation {
public:
    std::string getId() const override { return "3"; }
    std::string getName() const override;
    void execute() override;
};

class ChangeSpeedOperation : public IOperation {
public:
    std::string getId() const override { return "4"; }
    std::string getName() const override;
    void execute() override;
};

class RotateVideoOperation : public IOperation {
public:
    std::string getId() const override { return "5"; }
    std::string getName() const override;
    void execute() override;
};

class CompressVideoOperation : public IOperation {
public:
    std::string getId() const override { return "6"; }
    std::string getName() const override;
    void execute() override;
};

class AddWatermarkOperation : public IOperation {
public:
    std::string getId() const override { return "7"; }
    std::string getName() const override;
    void execute() override;
};

class AddSubtitlesOperation : public IOperation {
public:
    std::string getId() const override { return "8"; }
    std::string getName() const override;
    void execute() override;
};

class ExtractAudioOperation : public IOperation {
public:
    std::string getId() const override { return "9"; }
    std::string getName() const override;
    void execute() override;
};

class MergeVideoAudioOperation : public IOperation {
public:
    std::string getId() const override { return "a"; }
    std::string getName() const override;
    void execute() override;
};

class StripAudioOperation : public IOperation {
public:
    std::string getId() const override { return "b"; }
    std::string getName() const override;
    void execute() override;
};

class ConcatenateFilesOperation : public IOperation {
public:
    std::string getId() const override { return "c"; }
    std::string getName() const override;
    void execute() override;
};

class CreateGifOperation : public IOperation {
public:
    std::string getId() const override { return "d"; }
    std::string getName() const override;
    void execute() override;
};

class ExtractFramesOperation : public IOperation {
public:
    std::string getId() const override { return "e"; }
    std::string getName() const override;
    void execute() override;
};

class ShowFileInfoOperation : public IOperation {
public:
    std::string getId() const override { return "f"; }
    std::string getName() const override;
    void execute() override;
};

class CompareFilesOperation : public IOperation {
public:
    std::string getId() const override { return "g"; }
    std::string getName() const override;
    void execute() override;
};

} // namespace operations
} // namespace mr
