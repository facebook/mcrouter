/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "options.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <folly/Conv.h>
#include <folly/String.h>

#include "mcrouter/lib/fbi/debug.h"
#include "mcrouter/proxy.h"

using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace folly {

namespace {

template <class T> struct IsVector : public std::false_type {};
template <class T> struct IsVector<std::vector<T>> : public std::true_type {};

}  // anonymous namespace

template <class Tgt>
typename std::enable_if<IsVector<Tgt>::value, Tgt>::type
to(const string& str) {
  Tgt res;
  std::vector<std::string> parts;
  folly::split(",", str, parts, /* ignoreEmpty= */ true);
  for (const auto& it : parts) {
    res.push_back(folly::to<typename Tgt::value_type>(it));
  }
  return res;
}

template <class Tgt, class Src>
typename std::enable_if<IsVector<Src>::value && IsSomeString<Tgt>::value,
                        Tgt>::type
to(const Src& value) {
  return folly::join(",", value);
}

}  // folly

namespace facebook { namespace memcache {

namespace {

template <class T>
bool tryToString(const boost::any& value, std::string& res) {
  if (boost::any_cast<T*>(&value) != nullptr) {
    res = folly::to<std::string>(*boost::any_cast<T*>(value));
    return true;
  }
  return false;
}

std::string toString(const boost::any& value) {
  std::string res;
  bool ok = tryToString<int64_t>(value, res) ||
            tryToString<int>(value, res) ||
            tryToString<uint32_t>(value, res) ||
            tryToString<size_t>(value, res) ||
            tryToString<unsigned int>(value, res) ||
            tryToString<double>(value, res) ||
            tryToString<bool>(value, res) ||
            tryToString<std::string>(value, res) ||
            tryToString<vector<uint16_t>>(value, res);
  if (!ok) {
    throw std::logic_error("Unsupported option type: " +
      std::string(value.type().name()));
  }
  return res;
}

template <class T>
bool tryFromString(const std::string& str, const boost::any& value) {
  auto ptr = boost::any_cast<T*>(&value);
  if (ptr != nullptr) {
    **ptr = folly::to<T>(str);
    return true;
  }
  return false;
}

void fromString(const std::string& str, const boost::any& value) {
  bool ok = tryFromString<int64_t>(str, value) ||
            tryFromString<int>(str, value) ||
            tryFromString<uint32_t>(str, value) ||
            tryFromString<size_t>(str, value) ||
            tryFromString<unsigned int>(str, value) ||
            tryFromString<double>(str, value) ||
            tryFromString<bool>(str, value) ||
            tryFromString<std::string>(str, value) ||
            tryFromString<vector<uint16_t>>(str, value);

  if (!ok) {
    throw std::logic_error("Unsupported option type: " +
        std::string(value.type().name()));
  }
}

std::string optionTypeToString(McrouterOptionData::Type type) {
  switch (type) {
    case McrouterOptionData::Type::integer:
    case McrouterOptionData::Type::toggle:
      return "integer";
    case McrouterOptionData::Type::double_precision:
      return "double";
    case McrouterOptionData::Type::string:
      return "string";
    default:
      return "unknown";
  }
}

}  // anonymous namespace

unordered_map<string, string> McrouterOptionsBase::toDict() const {
  unordered_map<string, string> ret;

  forEach([&ret](const std::string& name,
                 McrouterOptionData::Type type,
                 const boost::any& value) {
    ret[name] = toString(value);
  });

  return ret;
}

vector<McrouterOptionError> McrouterOptionsBase::updateFromDict(
  const unordered_map<string, string>& new_opts) {

  vector<McrouterOptionError> errors;
  unordered_set<string> seen;

  forEach([&errors, &seen, &new_opts](const std::string& name,
                                      McrouterOptionData::Type type,
                                      const boost::any& value) {
    auto it = new_opts.find(name);
    if (it != new_opts.end()) {
      try {
        fromString(it->second, value);
      } catch (...) {
        McrouterOptionError e;
        e.requestedName = name;
        e.requestedValue = it->second;
        e.errorMsg = "Couldn't convert value to " + optionTypeToString(type);
        errors.push_back(std::move(e));
      }
      seen.insert(it->first);
    }
  });

  for (const auto& kv : new_opts) {
    if (seen.find(kv.first) == seen.end()) {
      McrouterOptionError e;
      e.requestedName = kv.first;
      e.requestedValue = kv.second;
      e.errorMsg = "Unknown option name";
      errors.push_back(e);
    }
  }

  return errors;
}

namespace options {

std::string substituteTemplates(std::string str) {
  return mcrouter::performOptionSubstitution(std::move(str));
}

McrouterOptions substituteTemplates(McrouterOptions opts) {
  opts.forEach([](const std::string& name,
                  McrouterOptionData::Type type,
                  const boost::any& value) {
    auto strPtr = boost::any_cast<std::string*>(&value);
    if (strPtr != nullptr) {
      **strPtr = mcrouter::performOptionSubstitution(**strPtr);
    }
  });

  return opts;
}

}  // facebook::memcache::options

}}  // facebook::memcache
