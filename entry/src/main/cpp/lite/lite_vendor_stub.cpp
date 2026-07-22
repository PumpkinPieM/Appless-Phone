#include "lite_vendor_adapter.h"

#include "lite_runtime.h"

namespace {
class StubVendorAdapter final : public LiteVendorAdapter {
public:
    void Initialize(const std::string&) override {
        throw LiteRuntimeError("NATIVE_UNAVAILABLE",
            "The vendor header/library was not compiled for this ABI.");
    }

    std::string Generate(const std::string&) override {
        throw LiteRuntimeError("NATIVE_UNAVAILABLE",
            "The vendor header/library was not compiled for this ABI.");
    }

    void Release() override {}
};
} // namespace

std::unique_ptr<LiteVendorAdapter> CreateLiteVendorAdapter() {
    return std::make_unique<StubVendorAdapter>();
}

bool IsLiteVendorAvailable() {
    return false;
}
