#include "RuntimeVarsData.h"

#include "folly/json.h"

namespace facebook { namespace memcache { namespace mcrouter {

RuntimeVarsData::RuntimeVarsData(folly::StringPiece json) {
  auto data = folly::parseJson(json);
  for (const auto& jiter: data.items()) {
    auto& key = jiter.first;
    auto& value = jiter.second;
    if (!key.isString()) {
      throw std::runtime_error("Bad config format, must have string keys");
    }
    configData_.emplace(key.asString().toStdString(), value);
  }
}

folly::dynamic
RuntimeVarsData::getVariableByName(const std::string& name) const {
  auto value = configData_.find(name);
  if (value == configData_.end()) {
    return nullptr;
  }
  return value->second;
}

}}} // facebook::memcache::mcrouter
