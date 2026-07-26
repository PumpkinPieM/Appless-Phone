#ifndef APPLESS_LITE_SERVER_MODEL_H
#define APPLESS_LITE_SERVER_MODEL_H

#include <memory>
#include <string>

namespace appless::lite_server {

struct GenerationResult {
    bool ok = false;
    std::string text;
    std::string error;
};

class Model {
public:
    virtual ~Model() = default;
    virtual bool Build(const std::string &config_path, std::string *error) = 0;
    virtual GenerationResult Generate(const std::string &prompt) = 0;
};

std::unique_ptr<Model> CreateModel();

}  // namespace appless::lite_server

#endif
