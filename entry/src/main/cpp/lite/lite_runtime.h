#pragma once

#include <future>
#include <memory>
#include <stdexcept>
#include <string>

class LiteRuntimeError : public std::runtime_error {
public:
    LiteRuntimeError(std::string code, const std::string& message)
        : std::runtime_error(message), code_(std::move(code)) {}

    const std::string& Code() const noexcept { return code_; }

private:
    std::string code_;
};

class LiteRuntime {
public:
    static LiteRuntime& Instance();

    std::future<void> Initialize(const std::string& configPath);
    std::future<std::string> Generate(const std::string& prompt);
    std::future<void> Release();
    bool IsVendorAvailable() const;

    ~LiteRuntime();
    LiteRuntime(const LiteRuntime&) = delete;
    LiteRuntime& operator=(const LiteRuntime&) = delete;

private:
    LiteRuntime();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
