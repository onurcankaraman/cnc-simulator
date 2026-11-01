#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace cnc {
using Json = nlohmann::json;
Json load_config(const std::string &directory);
std::string env(const char *name, const std::string &fallback = "");
} // namespace cnc
