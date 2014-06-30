/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include "ConfigPreprocessor.h"

#include <random>

#include "folly/Format.h"
#include "folly/Memory.h"
#include "folly/Random.h"
#include "folly/json.h"
#include "mcrouter/lib/fbi/cpp/util.h"
#include "mcrouter/lib/config/ImportResolverIf.h"

using folly::StringPiece;
using folly::dynamic;
using folly::make_unique;
using std::placeholders::_1;
using std::placeholders::_2;
using std::string;
using std::vector;

namespace facebook { namespace memcache {

namespace {

class NestedLimitGuard {
 public:
  explicit NestedLimitGuard(size_t& nestedLimit)
      : nestedLimit_(nestedLimit) {
    checkLogic(--nestedLimit_ > 0, "Too many nested macros. Check for cycles.");
  }
  ~NestedLimitGuard() {
    ++nestedLimit_;
  }
 private:
  size_t& nestedLimit_;
};

string asString(const dynamic& obj, const string& objName) {
  checkLogic(obj.isString(), "{} is not string", objName);
  return obj.asString().toStdString();
}

const dynamic& tryGet(const dynamic& obj, const string& key,
                      const string& objName) {
  auto it = obj.find(key);
  checkLogic(it != obj.items().end(), "{}: {} not found", objName, key);
  return it->second;
}

template <class Value>
const Value& tryGet(const std::unordered_map<string, Value>& map,
                    const string& key, const string& objName) {
  auto it = map.find(key);
  checkLogic(it != map.end(), "{}: {} not found", objName, key);
  return it->second;
}

/**
 * Finds matching bracket to one at position i in string s
 */
size_t matchingUnescapedBracket(StringPiece s, size_t i) {
  int diffOpenedClosed = 0;
  for (;i < s.size(); ++i) {
    if (s[i] == '\\') {
      ++i;
      continue;
    }
    if (s[i] == '(') {
      ++diffOpenedClosed;
    } else if (s[i] == ')') {
      --diffOpenedClosed;
    }

    // we are on closing bracket corresponding to ours
    if (diffOpenedClosed == 0) {
      return i;
    }
  }
  return string::npos;
}

// strip all C-like comments
string stripComments(StringPiece jsonC) {
  string result;
  enum class State {
    None,
    InString,
    InlineComment,
    LineComment
  } state = State::None;

  for (size_t i = 0; i < jsonC.size(); ++i) {
    auto s = jsonC.subpiece(i);
    switch (state) {
      case State::None:
        if (s.startsWith("/*")) {
          state = State::InlineComment;
          ++i;
          continue;
        } else if (s.startsWith("//")) {
          state = State::LineComment;
          ++i;
          continue;
        } else if (s.startsWith("\"")) {
          state = State::InString;
        }
        result.push_back(s[0]);
        break;
      case State::InString:
        if (s.startsWith("\\\"")) {
          result.push_back(s[0]);
          result.push_back(s[1]);
          ++i;
          continue;
        } else if (s.startsWith("\"")) {
          state = State::None;
        }
        result.push_back(s[0]);
        break;
      case State::InlineComment:
        if (s.startsWith("*/")) {
          state = State::None;
          ++i;
        }
        break;
      case State::LineComment:
        if (s.startsWith("\n")) {
          // skip the line break. It doesn't matter.
          state = State::None;
        }
        break;
      default:
        throw std::logic_error("Unknown comment state");
    }
  }
  return result;
}

size_t findUnescaped(folly::StringPiece str, char c, size_t start = 0) {
  for (size_t i = start; i < str.size(); ++i) {
    if (str[i] == '\\') {
      ++i;
      continue;
    }
    if (str[i] == c) {
      return i;
    }
  }
  return string::npos;
}

void unescapeInto(StringPiece from, string& to) {
  for (size_t i = 0; i < from.size(); ++i) {
    if (i + 1 < from.size() && from[i] == '\\') {
      to.push_back(from[i + 1]);
      ++i;
    } else {
      to.push_back(from[i]);
    }
  }
}

} // namespace

///////////////////////////////Macro////////////////////////////////////////////

class ConfigPreprocessor::Macro {
 public:
  typedef std::function<dynamic(const Context&)> Func;

  Macro(string name, const vector<dynamic>& params, dynamic result)
    : f_([result](const Context&) { return result; }),
      name_(std::move(name)) {

    initParams(params);
  }

  Macro(string name, const vector<dynamic>& params, Func f)
    : f_(std::move(f)),
      name_(std::move(name)) {

    initParams(params);
  }

  dynamic getResult(const Context& context) const {
    return f_(context);
  }

  Context getContext(vector<dynamic> paramValues) const {
    Context result;
    addDefaults(paramValues);
    for (size_t i = 0; i < paramNames_.size(); i++) {
      result.emplace(paramNames_[i], paramValues[i]);
    }
    return result;
  }

  Context getContext(const dynamic& macroObj) const {
    Context result;
    for (const auto& pn : paramNames_) {
      auto it = macroObj.find(pn);
      if (it == macroObj.items().end()) {
        result.emplace(pn, tryGet(defaultValues_, pn, "Default value"));
      } else {
        result.emplace(pn, it->second);
      }
    }
    return result;
  }

 private:
  Func f_;
  vector<string> paramNames_;
  std::unordered_map<string, dynamic> defaultValues_;
  string name_;

  void addDefaults(vector<dynamic>& params) const {
    auto minParamCnt = paramNames_.size() - defaultValues_.size();
    auto maxParamCnt = paramNames_.size();

    checkLogic(minParamCnt <= params.size(),
               "Too few arguments for macro {} expected at least {} got {}",
               name_, minParamCnt, params.size());

    checkLogic(params.size() <= maxParamCnt,
               "Too many arguments for macro {} expected at most {} got {}",
               name_, maxParamCnt, params.size());

    for (size_t i = params.size(); i < paramNames_.size(); i++) {
      params.push_back(tryGet(defaultValues_, paramNames_[i], "Default value"));
    }
  }

  void initParams(const vector<dynamic>& params) {
    bool needDefault = false;
    for (auto& param : params) {
      bool hasDefault = false;
      checkLogic(param.isString() || param.isObject(),
                 "Macro param is not string/object");

      if (param.isString()) { // param name
        paramNames_.push_back(param.asString().toStdString());
      } else { // object (name & default)
        auto name = asString(tryGet(param, "name", "Macro param object"),
                             "Macro param object name");
        paramNames_.push_back(name);

        if (param.find("default") != param.items().end()) {
          hasDefault = true;
          defaultValues_.emplace(name, param["default"]);
        }
      }

      checkLogic(hasDefault || !needDefault,
                 "Incorrect defaults in macro {}. All params after one with "
                 "default should also have defaults", name_);

      needDefault = needDefault || hasDefault;
    }
  }
};

///////////////////////////////////BuiltIns/////////////////////////////////////

class ConfigPreprocessor::BuiltIns {
 public:
  /**
   * Loads JSONM from external source via importResolver.
   * Usage: @import(path)
   */
  static dynamic importMacro(ConfigPreprocessor* p,
                             ImportResolverIf& importResolver,
                             const Context& ctx) {
    auto path = asString(tryGet(ctx, "path", "import"), "import path");
    // cache each result by path, so we won't import same path twice
    auto it = p->importCache_.find(path);
    if (it != p->importCache_.end()) {
      return it->second;
    }
    auto jsonC = importResolver.import(path);
    // result can contain comments, macros, etc.
    auto result = folly::parseJson(stripComments(jsonC));
    p->importCache_.emplace(std::move(path), result);
    return result;
  }

  /**
   * Casts it's argument to int.
   * Usage: @int(5)
   */
  static dynamic intMacro(const Context& ctx) {
    const auto& value = tryGet(ctx, "value", "int");
    // it will throw if value can not be casted to integer
    return dynamic(value.asInt());
  }

  /**
   * Casts it's argument to string.
   * Usage: @str(@int(5))
   */
  static dynamic strMacro(const Context& ctx) {
    const auto& value = tryGet(ctx, "value", "str");
    // it will throw if value can not be casted to string
    return dynamic(value.asString());
  }

  /**
   * Returns array of object keys
   * Usage: @keys(object)
   */
  static dynamic keysMacro(const Context& ctx) {
    const auto& dictionary = tryGet(ctx, "dictionary", "keys");
    checkLogic(dictionary.isObject(), "Keys: dictionary is not object");
    return dynamic(dictionary.keys().begin(), dictionary.keys().end());
  }

  /**
   * Returns array of object values
   * Usage: @values(object)
   */
  static dynamic valuesMacro(const Context& ctx) {
    const auto& dictionary = tryGet(ctx, "dictionary", "values");
    checkLogic(dictionary.isObject(), "Values: dictionary is not object");
    return dynamic(dictionary.values().begin(), dictionary.values().end());
  }

  /**
   * Special built-in that prevents expanding 'macroDef' objects unless we call
   * them. For internal use only, nobody should call it explicitly.
   */
  static dynamic macroDef(const folly::dynamic& json,
                          const Context& ctx) {
    // do not expand 'macroDef' until we call it
    return json;
  }

  /**
   * Special built-in that prevents expanding 'constDef' objects unless we parse
   * them. For internal use only, nobody should call it explicitly.
   */
  static dynamic constDef(const folly::dynamic& json,
                          const Context& ctx) {
    // do not expand 'constDef' until we parse it
    return json;
  }

  /**
   * Merges lists/objects.
   * Usage:
   * "type": "merge",
   * "params": [ list1, list2, list3, ... ]
   * or
   * "type": "merge",
   * "params": [ obj1, obj2, obj3, ... ]
   *
   * Returns single list/object which contains elements/properties of all
   * passed objects.
   * Note: properties of obj{N} will override properties of obj{N-1},
   */
  static dynamic merge(const ConfigPreprocessor* p,
                       const dynamic& json,
                       const Context& ctx) {
    auto mergeParams = p->expandMacros(tryGet(json, "params", "Merge"), ctx);

    checkLogic(mergeParams.isArray(), "Merge: 'params' is not array");
    checkLogic(!mergeParams.empty(), "Merge: empty params");

    // get first param, it will determine the result type (array or object)
    auto res = mergeParams[0];
    checkLogic(res.isArray() || res.isObject(),
               "Merge: param is not array/object");
    for (size_t i = 1; i < mergeParams.size(); ++i) {
      const auto& it = mergeParams[i];
      if (res.isArray()) {
        checkLogic(it.isArray(), "Merge: param is not array");
        for (const auto& inner : it) {
          res.push_back(inner);
        }
      } else { // object
        checkLogic(it.isObject(), "Merge: param is not object");
        // override properties
        for (const auto& inner : it.items()) {
          res.insert(inner.first, inner.second);
        }
      }
    }
    return res;
  }

  /**
   * Randomly shuffles list. Currently objects have no order, so it won't
   * have any effect if dictionary is obj.
   * Usage:
   * "type": "shuffle",
   * "dictionary": list
   * or
   * "type": "shuffle",
   * "dictionary": obj
   *
   * Returns list or object with randomly shuffled items.
   */
  static dynamic shuffle(const ConfigPreprocessor* p,
                         const dynamic& json,
                         const Context& ctx) {
    auto dictionary = p->expandMacros(tryGet(json, "dictionary", "Shuffle"),
                                      ctx);

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Shuffle: dictionary is not array/object");

    static std::minstd_rand engine(folly::randomNumberSeed());
    if (dictionary.isArray()) {
      for (size_t i = 0; i < dictionary.size(); ++i) {
        std::uniform_int_distribution<size_t> d(i, dictionary.size() - 1);
        std::swap(dictionary[i], dictionary[d(engine)]);
      }
    } // obj will be in random order, because it is currently unordered_map

    return dictionary;
  }

  /**
   * Selects element from list/object.
   * Usage:
   * "type": "select",
   * "dictionary": obj,
   * "key": "string"
   * or
   * "type": "select",
   * "dictionary": list,
   * "key": int
   *
   * Returns value of corresponding object property/list element
   */
  static dynamic select(const ConfigPreprocessor* p,
                        const dynamic& json,
                        const Context& ctx) {
    const auto key = p->expandMacros(tryGet(json, "key", "Select"), ctx);
    const auto dictionary =
      p->expandMacros(tryGet(json, "dictionary", "Select"), ctx);

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Select: dictionary is not array/object");

    if (dictionary.isObject()) {
      auto keyStr = asString(key, "Select: dictionary is object, key");
      // key should be in dictionary
      return tryGet(dictionary, keyStr, "Select");
    } else { // array
      checkLogic(key.isInt(), "Select: dictionary is array, key is not int");
      auto id = key.asInt();
      checkLogic(id >= 0 && size_t(id) < dictionary.size(),
                 "Select: index out of range");
      return dictionary[id];
    }
  }

  /**
   * Returns range from list/object.
   * Usage:
   * "type": "slice",
   * "dictionary": obj,
   * "from": string,
   * "to": string
   * or
   * "type": "slice",
   * "dictionary": list,
   * "from": int,
   * "to": int
   *
   * Returns:
   * - in case of list range of elements from <= id <= to.
   * - in case of object range of properties with keys from <= key <= to.
   * Note: from and to are inclusive
   */
  static dynamic slice(const ConfigPreprocessor* p,
                       const dynamic& json,
                       const Context& ctx) {
    auto from = p->expandMacros(tryGet(json, "from", "Slice"), ctx);
    auto to = p->expandMacros(tryGet(json, "to", "Slice"), ctx);
    auto dictionary = p->expandMacros(tryGet(json, "dictionary", "Slice"), ctx);

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Slice: dictionary is not array/object");

    if (dictionary.isObject()) {
      dynamic res = dynamic::object();
      auto fromKey = asString(from, "Slice: from");
      auto toKey = asString(to, "Slice: to");
      // since dictionary is unordered, we should iterate over it
      for (auto& it : dictionary.items()) {
        auto key = asString(it.first, "Slice: dictionary key");
        if (fromKey <= key && key <= toKey) {
          res.insert(it.first, it.second);
        }
      }
      return res;
    } else { // array
      dynamic res = {};
      checkLogic(from.isInt() && to.isInt(), "Slice: from/to not int");
      size_t fromId = from.asInt();
      size_t toId = to.asInt();
      for (size_t i = 0; i < dictionary.size(); ++i) {
        if (fromId <= i && i <= toId) {
          res.push_back(dictionary[i]);
        }
      }
      return res;
    }
  }

  /**
   * Tranforms keys and items of dictionary or elements of list.
   * Usage:
   * "type": "transform",
   * "dictionary": obj,
   * "itemTransform": macro with extended context
   * "keyTranform": macro with extended context (optional)
   * "itemName": string (optional, default: item)
   * "keyName": string (optional, default: key)
   * or
   * "type": "transform",
   * "dictionary": list,
   * "itemTranform": macro with extended context
   * "itemName": string (optional, default: item)
   *
   * Extended context is current context with two additional parameters:
   * %keyName% (key of current entry) and %itemName% (value of current entry)
   */
  static dynamic transform(const ConfigPreprocessor* p,
                           const dynamic& json,
                           const Context& ctx) {
    auto dictionary =
      p->expandMacros(tryGet(json, "dictionary", "Transform"), ctx);
    // item transform is required
    const auto& itemTransform = tryGet(json, "itemTransform", "Transform");

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Transform: dictionary is not array/object");

    auto itemName = json.find("itemName");
    string itemNameStr = itemName == json.items().end()
      ? "item" : asString(itemName->second, "Transform: itemName");

    if (dictionary.isObject()) {
      auto keyName = json.find("keyName");
      string keyNameStr = keyName == json.items().end()
        ? "key" : asString(keyName->second, "Transform: keyName");

      auto keyTransform = json.find("keyTransform");
      dynamic res = dynamic::object();
      auto extContext = ctx;
      for (const auto& item : dictionary.items()) {
        // add %key% and %item% to current context.
        extContext.erase(keyNameStr);
        extContext.erase(itemNameStr);
        extContext.emplace(keyNameStr, item.first);
        extContext.emplace(itemNameStr, item.second);

        auto nKey = keyTransform == json.items().end()
          ? item.first
          : p->expandMacros(keyTransform->second, extContext);

        res.insert(std::move(nKey), p->expandMacros(itemTransform, extContext));
      }
      return res;
    } else { // array
      dynamic res = {};
      auto extContext = ctx;
      for (const auto& item : dictionary) {
        // add %item% to current context.
        extContext.erase(itemNameStr);
        extContext.emplace(itemNameStr, item);
        res.push_back(p->expandMacros(itemTransform, extContext));
      }
      return res;
    }
  }
};

///////////////////////////////ConfigPreprocessor///////////////////////////////

ConfigPreprocessor::ConfigPreprocessor(ImportResolverIf& importResolver,
                                       Context globals,
                                       size_t nestedLimit)
  : globals_(std::move(globals)),
    nestedLimit_(nestedLimit) {

  addBuiltInMacro("import", { "path" },
    std::bind(&BuiltIns::importMacro, this, std::ref(importResolver), _1));

  addBuiltInMacro("int", { "value" }, &BuiltIns::intMacro);

  addBuiltInMacro("str", { "value" }, &BuiltIns::strMacro);

  addBuiltInMacro("keys", { "dictionary" }, &BuiltIns::keysMacro);

  addBuiltInMacro("values", { "dictionary" }, &BuiltIns::valuesMacro);

  builtInCalls_.emplace("macroDef", &BuiltIns::macroDef);

  builtInCalls_.emplace("constDef", &BuiltIns::constDef);

  builtInCalls_.emplace("merge", std::bind(&BuiltIns::merge, this, _1, _2));

  builtInCalls_.emplace("shuffle", std::bind(&BuiltIns::shuffle, this, _1, _2));

  builtInCalls_.emplace("select", std::bind(&BuiltIns::select, this, _1, _2));

  builtInCalls_.emplace("slice", std::bind(&BuiltIns::slice, this, _1, _2));

  builtInCalls_.emplace("transform",
    std::bind(&BuiltIns::transform, this, _1, _2));
}

void ConfigPreprocessor::addBuiltInMacro(string name, vector<dynamic> params,
                                         Macro::Func func) {
  macros_.emplace(name,
    make_unique<Macro>(name, std::move(params), std::move(func)));
}

dynamic ConfigPreprocessor::replaceParams(StringPiece str,
                                          const Context& context) const {
  string buf;
  while (!str.empty()) {
    auto pos = findUnescaped(str, '%');
    if (pos == string::npos) {
      unescapeInto(str, buf);
      break;
    }
    unescapeInto(str.subpiece(0, pos), buf);

    auto nextPos = findUnescaped(str, '%', pos + 1);
    checkLogic(nextPos != string::npos,
               "Odd number of percent signs in string");

    string paramName;
    unescapeInto(str.subpiece(pos + 1, nextPos - pos - 1), paramName);
    // first check current context, then global context
    auto paramIt = context.find(paramName);
    auto substitution = paramIt == context.end()
      ? tryGet(globals_, paramName, "Param in string")
      : paramIt->second;

    if (buf.empty() && nextPos == str.size() - 1) {
      // whole string is parameter. May be substituted to object.
      return substitution;
    } else {
      // param inside string e.g. a%param%b
      buf += asString(substitution, "Param in string");
    }
    str = str.subpiece(nextPos + 1);
  }
  return buf;
}

vector<dynamic>
ConfigPreprocessor::getCallParams(StringPiece str,
                                  const Context& context) const {
  // all params are substituted. But inner macro calls are possible.
  std::vector<dynamic> result;
  while (true) {
    if (str.empty()) {
      // it is one parameter - empty string e.g. @a() is call with one parameter
      result.push_back("");
      break;
    }

    auto commaPos = findUnescaped(str, ',');
    if (commaPos == string::npos) {
      // no commas - only one param;
      result.push_back(expandStringMacro(str, context));
      break;
    }

    auto firstOpened = findUnescaped(str, '(');
    // macro call with params
    if (firstOpened != string::npos && commaPos > firstOpened) {
      // first param is inner macro with params
      auto closing = matchingUnescapedBracket(str, firstOpened);
      checkLogic(closing != string::npos, "Brackets do not match: {}", str);

      if (closing == str.size() - 1) {
        // no more params, only single inner macro
        result.push_back(expandStringMacro(str, context));
        break;
      }

      checkLogic(str[closing + 1] == ',',
                 "No comma after closing bracket: {}", str);
      commaPos = closing + 1;
    }

    // add first param
    auto firstParam = str.subpiece(0, commaPos);
    result.push_back(expandStringMacro(firstParam, context));
    // and process next params
    str = str.subpiece(commaPos + 1);
  }

  return result;
}

dynamic ConfigPreprocessor::expandStringMacro(StringPiece str,
                                              const Context& context) const {
  NestedLimitGuard nestedGuard(nestedLimit_);

  if (str.empty()) {
    return "";
  }

  if (str[0] != '@') {
    return replaceParams(str, context);
  }

  // macro in string
  StringPiece name;
  vector<dynamic> innerParams;

  auto paramStart = findUnescaped(str, '(');
  if (paramStart != string::npos) {
    // macro in string with params e.g. @a()
    checkLogic(str[str.size() - 1] == ')', "No closing bracket for call");

    name = str.subpiece(1, paramStart - 1);

    // get parameters of this macro
    auto innerStr = str.subpiece(paramStart + 1, str.size() - paramStart - 2);
    innerParams = getCallParams(innerStr, context);
  } else {
    // macro in string without params e.g. @a
    name = str.subpiece(1);
  }

  auto substName = replaceParams(name, context);
  auto nameStr = asString(substName, "Macro name");

  const auto& inner = tryGet(macros_, nameStr, "Macro");
  // substitute inner params
  auto innerContext = inner->getContext(innerParams);
  for (auto& it : innerContext) {
    it.second = expandMacros(it.second, context);
  }
  return expandMacros(inner->getResult(innerContext), innerContext);
}

dynamic ConfigPreprocessor::expandMacros(const dynamic& json,
                                         const Context& context) const {
  NestedLimitGuard nestedGuard(nestedLimit_);

  if (json.isString()) {
    // look for macros in string
    auto str = json.asString();
    return expandStringMacro(str, context);
  } else if (json.isObject()) {
    // check for built-in calls and long-form macros
    auto typeIt = json.find("type");
    if (typeIt != json.items().end()) {
      auto type = expandMacros(typeIt->second, context);
      if (type.isString()) {
        auto typeStr = type.asString().toStdString();
        // built-in call
        auto builtInIt = builtInCalls_.find(typeStr);
        if (builtInIt != builtInCalls_.end()) {
          return builtInIt->second(json, context);
        }
        // long form macro substitution
        auto macroIt = macros_.find(typeStr);
        if (macroIt != macros_.end()) {
          const auto& inner = macroIt->second;
          auto innerContext = inner->getContext(json);
          for (auto& it : innerContext) {
            it.second = expandMacros(it.second, context);
          }
          return expandMacros(inner->getResult(innerContext), innerContext);
        }
      }
    }

    // raw object
    dynamic result = dynamic::object();
    for (auto& it : json.items()) {
      auto key = asString(expandMacros(it.first, context), "object key");
      result.insert(key, expandMacros(it.second, context));
    }
    return result;
  } else if (json.isArray()) {
    dynamic result = {};
    for (auto& it : json) {
      result.push_back(expandMacros(it, context));
    }
    return result;
  } else {
    // some number or other type of json. Return 'as is'.
    return json;
  }
}

dynamic ConfigPreprocessor::getConfigWithoutMacros(
    StringPiece jsonC,
    ImportResolverIf& importResolver,
    std::unordered_map<string, dynamic> globalParams,
    size_t nestedLimit) {

  auto config = folly::parseJson(stripComments(jsonC));
  checkLogic(config.isObject(), "config is not object");

  ConfigPreprocessor prep(importResolver, std::move(globalParams), nestedLimit);

  // parse and add consts
  auto constsIt = config.find("consts");
  if (constsIt != config.items().end()) {
    auto consts = prep.expandMacros(constsIt->second, globalParams);
    checkLogic(consts.isArray(), "config consts is not array");
    for (const auto& it : consts) {
      checkLogic(it.isObject(), "constDef is not object");
      auto type = asString(tryGet(it, "type", "constDef"), "constDef type");
      checkLogic(type == "constDef", "constDef has invalid type: {}", type);
      auto name = asString(tryGet(it, "name", "constDef"), "constDef name");
      const auto& result = tryGet(it, "result", "constDef");
      prep.globals_.emplace(name, prep.expandMacros(result, globalParams));
    }
    config.erase("consts");
  }

  // parse and add macros
  auto macrosIt = config.find("macros");
  if (macrosIt != config.items().end()) {
    auto macros = prep.expandMacros(macrosIt->second, globalParams);
    checkLogic(macros.isObject(), "config macros is not object");
    config.erase("macros");

    for (auto& it : macros.items()) {
      auto key = asString(it.first, "macros key");
      auto& obj = it.second;
      checkLogic(obj.isObject(), "macroDef is not object");
      auto objType = asString(tryGet(obj, "type", "macroDef"), "macroDef type");
      checkLogic(objType == "macroDef",
                 "macroDef has invalid type: {}", objType);

      const auto& res = tryGet(obj, "result", "Macro definition");
      vector<dynamic> params;
      auto paramsIt = obj.find("params");
      if (paramsIt != obj.items().end()) {
        checkLogic(paramsIt->second.isArray(), "macroDef params is not array");
        for (auto& paramObj : paramsIt->second) {
          params.push_back(paramObj);
        }
      }
      prep.macros_.emplace(key, make_unique<Macro>(key, params, res));
    }
  }

  return prep.expandMacros(config, globalParams);
}

}}  // facebook::memcache
