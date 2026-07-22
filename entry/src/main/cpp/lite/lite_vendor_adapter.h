#pragma once

#include <memory>
#include <string>

class LiteVendorAdapter {
public:
    virtual ~LiteVendorAdapter() = default;
    virtual void Initialize(const std::string& configPath) = 0;
    virtual std::string Generate(const std::string& prompt) = 0;
    virtual void Release() = 0;
};

std::unique_ptr<LiteVendorAdapter> CreateLiteVendorAdapter();
bool IsLiteVendorAvailable();
