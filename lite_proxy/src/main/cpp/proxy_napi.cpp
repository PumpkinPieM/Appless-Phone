#include "proxy_gateway.h"

#include "napi/native_api.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace appless::lite_proxy {
namespace {

bool GetNamedValue(napi_env env, napi_value object, const char *name, napi_value *value)
{
    bool has_property = false;
    return napi_has_named_property(env, object, name, &has_property) == napi_ok && has_property &&
        napi_get_named_property(env, object, name, value) == napi_ok;
}

bool ReadString(napi_env env, napi_value object, const char *name, std::string *value, std::string *error)
{
    napi_value property = nullptr;
    if (!GetNamedValue(env, object, name, &property)) {
        *error = std::string("missing option: ") + name;
        return false;
    }
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_string) {
        *error = std::string(name) + " must be a string";
        return false;
    }
    std::size_t length = 0;
    if (napi_get_value_string_utf8(env, property, nullptr, 0, &length) != napi_ok) {
        *error = std::string("failed to read ") + name;
        return false;
    }
    std::string result(length + 1, '\0');
    std::size_t copied = 0;
    if (napi_get_value_string_utf8(env, property, result.data(), result.size(), &copied) != napi_ok) {
        *error = std::string("failed to read ") + name;
        return false;
    }
    result.resize(copied);
    *value = std::move(result);
    return true;
}

bool ReadUnsigned(napi_env env, napi_value object, const char *name, std::uint64_t maximum,
                  std::uint64_t *value, std::string *error)
{
    napi_value property = nullptr;
    if (!GetNamedValue(env, object, name, &property)) {
        *error = std::string("missing option: ") + name;
        return false;
    }
    napi_valuetype type = napi_undefined;
    double number = 0;
    if (napi_typeof(env, property, &type) != napi_ok || type != napi_number ||
        napi_get_value_double(env, property, &number) != napi_ok || !std::isfinite(number) || number < 0 ||
        number > static_cast<double>(maximum) || std::floor(number) != number) {
        *error = std::string(name) + " must be a non-negative integer no greater than " + std::to_string(maximum);
        return false;
    }
    *value = static_cast<std::uint64_t>(number);
    return true;
}

void SetString(napi_env env, napi_value object, const char *name, const std::string &value)
{
    napi_value property = nullptr;
    (void)napi_create_string_utf8(env, value.c_str(), value.size(), &property);
    (void)napi_set_named_property(env, object, name, property);
}

void SetBool(napi_env env, napi_value object, const char *name, bool value)
{
    napi_value property = nullptr;
    (void)napi_get_boolean(env, value, &property);
    (void)napi_set_named_property(env, object, name, property);
}

void SetNumber(napi_env env, napi_value object, const char *name, double value)
{
    napi_value property = nullptr;
    (void)napi_create_double(env, value, &property);
    (void)napi_set_named_property(env, object, name, property);
}

napi_value StatusValue(napi_env env, const ProxyStatus &status)
{
    napi_value result = nullptr;
    (void)napi_create_object(env, &result);
    SetString(env, result, "state", status.state);
    SetBool(env, result, "running", status.running);
    SetBool(env, result, "serverConnected", status.server_connected);
    SetNumber(env, result, "httpPort", status.http_port);
    SetNumber(env, result, "reversePort", status.reverse_port);
    SetString(env, result, "baseUrl", status.base_url);
    SetString(env, result, "authToken", status.auth_token);
    SetNumber(env, result, "pendingRequests", static_cast<double>(status.pending_requests));
    SetString(env, result, "lastError", status.last_error);
    return result;
}

napi_value Start(napi_env env, napi_callback_info info)
{
    std::size_t argument_count = 1;
    napi_value arguments[1] = {nullptr};
    if (napi_get_cb_info(env, info, &argument_count, arguments, nullptr, nullptr) != napi_ok || argument_count != 1) {
        napi_throw_type_error(env, nullptr, "start(options) requires one options object");
        return nullptr;
    }
    napi_valuetype type = napi_undefined;
    if (napi_typeof(env, arguments[0], &type) != napi_ok || type != napi_object) {
        napi_throw_type_error(env, nullptr, "start(options) requires one options object");
        return nullptr;
    }
    ProxyOptions options;
    std::string error;
    std::uint64_t number = 0;
    if (!ReadUnsigned(env, arguments[0], "httpPort", 65535, &number, &error)) {
        napi_throw_range_error(env, nullptr, error.c_str());
        return nullptr;
    }
    options.http_port = static_cast<std::uint16_t>(number);
    if (!ReadUnsigned(env, arguments[0], "reversePort", 65535, &number, &error)) {
        napi_throw_range_error(env, nullptr, error.c_str());
        return nullptr;
    }
    options.reverse_port = static_cast<std::uint16_t>(number);
    if (!ReadString(env, arguments[0], "authToken", &options.auth_token, &error)) {
        napi_throw_type_error(env, nullptr, error.c_str());
        return nullptr;
    }
    if (!ReadUnsigned(env, arguments[0], "requestTimeoutMs", 30U * 60U * 1000U, &number, &error)) {
        napi_throw_range_error(env, nullptr, error.c_str());
        return nullptr;
    }
    options.request_timeout_ms = static_cast<std::uint32_t>(number);
    if (!ReadUnsigned(env, arguments[0], "maxBodyBytes", 64U * 1024U * 1024U, &number, &error)) {
        napi_throw_range_error(env, nullptr, error.c_str());
        return nullptr;
    }
    options.max_body_bytes = static_cast<std::size_t>(number);

    if (!ProxyGateway::Instance().Start(options, &error)) {
        napi_throw_error(env, nullptr, error.c_str());
        return nullptr;
    }
    return StatusValue(env, ProxyGateway::Instance().Status());
}

napi_value GetStatus(napi_env env, napi_callback_info info)
{
    (void)info;
    return StatusValue(env, ProxyGateway::Instance().Status());
}

napi_value Stop(napi_env env, napi_callback_info info)
{
    (void)info;
    ProxyGateway::Instance().Stop();
    napi_value result = nullptr;
    (void)napi_get_undefined(env, &result);
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    const napi_property_descriptor properties[] = {
        {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"getStatus", nullptr, GetStatus, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    (void)napi_define_properties(env, exports, sizeof(properties) / sizeof(properties[0]), properties);
    return exports;
}

}  // namespace
}  // namespace appless::lite_proxy

static napi_module liteProxyModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = appless::lite_proxy::Init,
    .nm_modname = "lite_proxy",
    .nm_priv = nullptr,
    .reserved = {nullptr},
};

extern "C" __attribute__((constructor)) void RegisterLiteProxyModule()
{
    napi_module_register(&liteProxyModule);
}
