/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ConfigPreprocessor.h"

#include <random>
#include <unordered_set>

#include <folly/Format.h>
#include <folly/json.h>
#include <folly/Memory.h>
#include <folly/Random.h>

#include "mcrouter/lib/config/ImportResolverIf.h"
#include "mcrouter/lib/fbi/cpp/util.h"

using folly::StringPiece;
using folly::dynamic;
using folly::json::stripComments;
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

string asString(const dynamic& obj, StringPiece objName) {
  checkLogic(obj.isString(), "{} is {}, string expected",
             objName, obj.typeName());
  return obj.stringPiece().str();
}

const dynamic& tryGet(const dynamic& obj, const dynamic& key,
                      StringPiece objName) {
  auto it = obj.find(key);
  checkLogic(it != obj.items().end(), "{}: '{}' not found", objName, key);
  return it->second;
}

dynamic moveGet(dynamic& obj, const dynamic& key, StringPiece objName) {
  auto it = obj.get_ptr(key);
  checkLogic(it, "{}: '{}' not found", objName, key);
  return std::move(*it);
}

template <class Value>
const Value& tryGet(const std::unordered_map<string, Value>& map,
                    const string& key, StringPiece objName) {
  auto it = map.find(key);
  checkLogic(it != map.end(), "{}: '{}' not found", objName, key);
  return it->second;
}

StringPiece trim(StringPiece sp) {
  while (!sp.empty() && sp.front() == ' ') {
    sp.pop_front();
  }
  while (!sp.empty()) {
    if (sp.back() != ' ') {
      break;
    }
    // the space should be unescaped, i.e. number of consecutive slashes
    // should be even
    if (sp.size() > 1) {
      auto it = sp.end() - 2;
      size_t cntSlashes = 0;
      while (it != sp.begin() && *it == '\\') {
        ++cntSlashes;
        --it;
      }
      if (*it == '\\') {
        ++cntSlashes;
      }
      if (cntSlashes > 0) {
        if (cntSlashes % 2 == 0) {
          sp.pop_back();
        }
        break;
      }
    }
    sp.pop_back();
  }
  return sp;
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

size_t findUnescaped(folly::StringPiece str, char c) {
  for (auto it = str.begin(); it != str.end(); ++it) {
    if (*it == '\\') {
      ++it;
      continue;
    }
    if (*it == c) {
      return it - str.begin();
    }
  }
  return string::npos;
}

size_t unescapeUntil(StringPiece from, string& to, char c) {
  for (auto it = from.begin(); it != from.end(); ++it) {
    if (*it == '\\') {
      if (++it == from.end()) {
        return string::npos;
      }
      to.push_back(*it);
      continue;
    }
    if (*it == c) {
      return it - from.begin();
    }
    to.push_back(*it);
  }
  return string::npos;
}

} // namespace

const ConfigPreprocessor::Context ConfigPreprocessor::emptyContext_{};

///////////////////////////////Macro////////////////////////////////////////////

class ConfigPreprocessor::Macro {
 public:
  typedef std::function<dynamic(Context)> Func;

  Macro(string name, const vector<dynamic>& params, Func f)
    : f_(std::move(f)),
      name_(std::move(name)) {

    initParams(params);
  }

  dynamic getResult(Context context) const {
    return f_(std::move(context));
  }

  Context getContext(vector<dynamic> paramValues) const {
    checkLogic(minParamCnt_ <= paramValues.size(),
               "Too few arguments for macro {}. Expected at least {} got {}",
               name_, minParamCnt_, paramValues.size());

    checkLogic(paramValues.size() <= maxParamCnt_,
               "Too many arguments for macro {}. Expected at most {} got {}",
               name_, maxParamCnt_, paramValues.size());

    Context result;
    for (size_t i = 0; i < paramValues.size(); i++) {
      result.emplace(paramNames_[i].first, std::move(paramValues[i]));
    }

    for (size_t i = paramValues.size(); i < paramNames_.size(); i++) {
      result.emplace(paramNames_[i].first, paramNames_[i].second);
    }
    return result;
  }

  Context getContext(dynamic macroObj) const {
    Context result;
    for (const auto& pn : paramNames_) {
      auto it = macroObj.get_ptr(pn.first);
      if (!it) {
        checkLogic(!pn.second.isNull(),
                   "Macro call {}: '{}' parameter not found", name_, pn.first);
        result.emplace(pn.first, pn.second);
      } else {
        result.emplace(pn.first, std::move(*it));
      }
    }
    return result;
  }

 private:
  Func f_;
  vector<std::pair<string, dynamic>> paramNames_;
  size_t maxParamCnt_{0};
  size_t minParamCnt_{0};
  const string name_;

  void initParams(const vector<dynamic>& params) {
    maxParamCnt_ = minParamCnt_ = params.size();
    bool needDefault = false;
    for (const auto& param : params) {
      bool hasDefault = false;
      checkLogic(param.isString() || param.isObject(),
                 "Macro param is not string/object");

      if (param.isString()) { // param name
        paramNames_.emplace_back(param.stringPiece().str(), nullptr);
      } else { // object (name & default)
        auto name = asString(tryGet(param, "name", "Macro param object"),
                             "Macro param object name");

        auto defaultIt = param.find("default");
        if (defaultIt != param.items().end()) {
          hasDefault = true;
          --minParamCnt_;
          paramNames_.emplace_back(name, defaultIt->second);
        }
      }

      checkLogic(hasDefault || !needDefault,
                 "Incorrect defaults in macro {}. All params after one with "
                 "default should also have defaults", name_);

      needDefault = needDefault || hasDefault;
    }
  }
};

////////////////////////////////////Const///////////////////////////////////////

class ConfigPreprocessor::Const {
 public:
  Const(const ConfigPreprocessor& prep,
        std::string name,
        dynamic result)
    : prep_(prep),
      name_(std::move(name)),
      result_(std::move(result)) {
  }

  const dynamic& getResult() const {
    if (!expanded_) {
      try {
        result_ = prep_.expandMacros(std::move(result_),
                                     ConfigPreprocessor::emptyContext_);
      } catch (const std::logic_error& e) {
        throw std::logic_error("Const '" + name_ + "':\n" + e.what());
      }
      expanded_ = true;
    }
    return result_;
  }
 private:
  mutable bool expanded_{false};
  const ConfigPreprocessor& prep_;
  const std::string name_;
  mutable dynamic result_;
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
                             Context ctx) {
    auto path = asString(ctx.at("path"), "import path");
    // cache each result by path, so we won't import same path twice
    auto it = p->importCache_.find(path);
    if (it != p->importCache_.end()) {
      return it->second;
    }
    dynamic result = nullptr;
    try {
      auto jsonC = importResolver.import(path);
      // result may contain comments, macros, etc.
      result = p->expandMacros(parseJsonString(stripComments(jsonC)),
                               ConfigPreprocessor::emptyContext_);
    } catch (const std::exception& e) {
      throw std::logic_error("Import '" + path + "':\n" + e.what());
    }
    p->importCache_.emplace(std::move(path), result);
    return result;
  }

  /**
   * Casts its argument to int.
   * Usage: @int(5)
   */
  static dynamic intMacro(Context ctx) {
    try {
      return ctx.at("value").asInt();
    } catch (const std::exception& e) {
      throw std::logic_error(string("Can not cast to int:\n") + e.what());
    }
  }

  /**
   * Casts its argument to string.
   * Usage: @str(@int(5))
   */
  static dynamic strMacro(Context ctx) {
    try {
      return ctx.at("value").asString();
    } catch (const std::exception& e) {
      throw std::logic_error(string("Can not cast to string:\n") + e.what());
    }
  }

  /**
   * Casts its argument to boolean.
   * Usage: @bool(false); @bool(true); @bool(@int(0)); @bool(@int(1))
   */
  static dynamic boolMacro(Context ctx) {
    try {
      return ctx.at("value").asBool();
    } catch (const std::exception& e) {
      throw std::logic_error(string("Can not cast to boolean:\n") + e.what());
    }
  }

  /**
   * Returns array of object keys
   * Usage: @keys(object)
   */
  static dynamic keysMacro(Context ctx) {
    const auto& dictionary = ctx.at("dictionary");
    checkLogic(dictionary.isObject(), "Keys: dictionary is not object");
    return dynamic(dictionary.keys().begin(), dictionary.keys().end());
  }

  /**
   * Returns array of object values
   * Usage: @values(object)
   */
  static dynamic valuesMacro(Context ctx) {
    const auto& dictionary = ctx.at("dictionary");
    checkLogic(dictionary.isObject(), "Values: dictionary is not object");
    return dynamic(dictionary.values().begin(), dictionary.values().end());
  }

  /**
   * Merges lists/objects/strings.
   * Usage:
   * "type": "merge",
   * "params": [ list1, list2, list3, ... ]
   * or
   * "type": "merge",
   * "params": [ obj1, obj2, obj3, ... ]
   * or
   * "type": "merge"
   * "params": [ str1, str2, str3, ... ]
   *
   * Returns single list/object which contains elements/properties of all
   * passed objects.
   * Note: properties of obj{N} will override properties of obj{N-1}.
   * In case params are strings, "merge" concatenates them.
   */
  static dynamic mergeMacro(Context ctx) {
    auto& mergeParams = ctx.at("params");

    checkLogic(mergeParams.isArray(), "Merge: 'params' is not array");
    checkLogic(!mergeParams.empty(), "Merge: empty params");

    // get first param, it will determine the result type (array or object)
    auto res = std::move(mergeParams[0]);
    checkLogic(res.isArray() || res.isObject() || res.isString(),
               "Merge: first param is not array/object/string");
    for (size_t i = 1; i < mergeParams.size(); ++i) {
      auto& it = mergeParams[i];
      if (res.isArray()) {
        checkLogic(it.isArray(), "Merge: param {} is not an array", i);
        for (auto& inner : it) {
          res.push_back(std::move(inner));
        }
      } else if (res.isObject()) {
        checkLogic(it.isObject(), "Merge: param {} is not an object", i);
        // override properties
        for (auto& inner : it.items()) {
          auto& key = const_cast<dynamic&>(inner.first);
          auto& value = const_cast<dynamic&>(inner.second);
          res.insert(std::move(key), std::move(value));
        }
      } else { // string
        checkLogic(it.isString(), "Merge: param {} is not a string", i);
        res.getString() += it.getString();
      }
    }
    return res;
  }

  /**
   * Randomly shuffles list. Currently objects have no order, so it won't
   * have any effect if dictionary is obj.
   * Usage: @shuffle(list) or @shuffle(obj)
   *
   * Returns list or object with randomly shuffled items.
   */
  static dynamic shuffleMacro(Context ctx) {
    auto& dictionary = ctx.at("dictionary");

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
   * Selects element(s) from list/object.
   * Usage: @select(obj,string/array of strings) or
   *        @select(list,int/array of ints)
   *
   * Returns value(s) of corresponding object property(ies)/list element(s)
   */
  static dynamic selectMacro(Context ctx) {
    auto& dictionary = ctx.at("dictionary");
    auto& key = ctx.at("key");

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Select: dictionary is not array/object");

    if (dictionary.isObject()) {
      checkLogic(key.isString() || key.isArray(),
                 "Select: dictionary is an object, key is not array/string");
      if (key.isString()) {
        // key should be in dictionary
        return moveGet(dictionary, key, "Select");
      } else { // array
        dynamic result = dynamic::object();
        for (size_t i = 0; i < key.size(); ++i) {
          auto& it = key[i];
          checkLogic(it.isString(),
                     "Select: dictionary is an object, key item is not a "
                     "string");
          checkLogic(result.find(it) == result.items().end(),
                     "Select: duplicate key");
          auto val = moveGet(dictionary, it, "Select");
          result.insert(std::move(it), std::move(val));
        }
        return result;
      }
    } else { // array
      checkLogic(key.isInt() || key.isArray(),
                 "Select: dictionary is an array, key is not array/integer");
      if (key.isInt()) {
        auto id = key.getInt();
        checkLogic(id >= 0 && size_t(id) < dictionary.size(),
                   "Select: index out of range");
        return std::move(dictionary[id]);
      } else { // array
        dynamic result = {};
        std::unordered_set<size_t> indicies;
        for (size_t i = 0; i < key.size(); ++i) {
          checkLogic(key[i].isInt(),
                     "Select: dictionary is an array, key item is not an "
                     "integer");
          auto id = key[i].getInt();
          checkLogic(id >= 0 && size_t(id) < dictionary.size(),
                     "Select: index out of range");
          checkLogic(indicies.insert(id).second, "Select: duplicate index");
          result.push_back(std::move(dictionary[id]));
        }
        return result;
      }
    }
  }

  /**
   * Returns range from list/object/string.
   * Usage:
   * "type": "slice",
   * "dictionary": obj,
   * "from": string,
   * "to": string
   * or
   * "type": "slice",
   * "dictionary": list/string,
   * "from": int,
   * "to": int
   *
   * Returns:
   * - in case of list range of elements from <= id <= to.
   * - in case of object range of properties with keys from <= key <= to.
   * - in case of string substring [from, to]
   * Note: from and to are inclusive
   */
  static dynamic sliceMacro(Context ctx) {
    const auto& from = ctx.at("from");
    const auto& to = ctx.at("to");
    auto& dict = ctx.at("dictionary");

    checkLogic(dict.isObject() || dict.isArray() || dict.isString(),
               "Slice: dictionary is not array/object/string");

    if (dict.isObject()) {
      dynamic res = dynamic::object();
      checkLogic(from.isString(), "Slice: from is not a string");
      checkLogic(to.isString(), "Slice: to is not a string");
      auto fromKey = from.stringPiece();
      auto toKey = to.stringPiece();
      // since dictionary is unordered, we should iterate over it
      for (auto& it : dict.items()) {
        auto& key = const_cast<dynamic&>(it.first);
        auto& value = const_cast<dynamic&>(it.second);
        if (fromKey <= key.stringPiece() && key.stringPiece() <= toKey) {
          res.insert(std::move(key), std::move(value));
        }
      }
      return res;
    } else if (dict.isArray()) {
      dynamic res = {};
      checkLogic(from.isInt() && to.isInt(), "Slice: from/to is not an int");
      auto fromId = std::max(0L, from.asInt());
      auto toId = std::min(to.asInt() + 1, (int64_t)dict.size());
      for (auto i = fromId; i < toId; ++i) {
        res.push_back(std::move(dict[i]));
      }
      return res;
    } else { // string
      string res;
      auto dictStr = dict.stringPiece();
      checkLogic(from.isInt() && to.isInt(), "Slice: from/to is not an int");
      auto fromId = std::max(0L, from.asInt());
      auto toId = std::min(to.asInt() + 1, (int64_t)dict.size());
      for (auto i = fromId; i < toId; ++i) {
        res += dictStr[i];
      }
      return res;
    }
  }

  /**
   * Returns `dictionary` with `key` set to `value` i.e.
   *   dictionary[key] = value;
   *   return dictionary;
   *
   * Usage:
   * "type": "set",
   * "dictionary": array or object,
   * "key": string or int,
   * "value": jsonm
   */
  static dynamic setMacro(Context ctx) {
    const auto& key = ctx.at("key");
    auto& value = ctx.at("value");
    auto& dict = ctx.at("dictionary");

    checkLogic(dict.isObject() || dict.isArray(),
               "Set: dictionary is not array/object");

    if (dict.isObject()) {
      checkLogic(key.isString(), "Set: key is not a string");
      dict[key.getString()] = std::move(value);
      return dict;
    } else { // array
      checkLogic(key.isInt(), "Set: key is not an int");
      auto id = key.getInt();
      checkLogic(0 <= id && static_cast<size_t>(id) < dict.size(),
                 "Set: key '{}' is out of range [0..{})", id, dict.size());
      dict[id] = std::move(value);
      return dict;
    }
  }

  /**
   * Returns list of integers [from, from+1, ... to]
   */
  static dynamic rangeMacro(Context ctx) {
    const auto& from = ctx.at("from");
    const auto& to = ctx.at("to");
    checkLogic(from.isInt(), "Range: from is not an integer");
    checkLogic(to.isInt(), "Range: to is not an integer");
    dynamic result = {};
    for (auto i = from.asInt(); i <= to.asInt(); ++i) {
      result.push_back(i);
    }
    return result;
  }

  /**
   * Returns true if:
   * - dictionary is object which contains key == key (param)
   * - dictionary is array which contains item == key (param)
   * - dictionary is string which contains key (param) as a substring.
   * Usage: @contains(dictionary,value)
   */
  static dynamic containsMacro(Context ctx) {
    const auto& dictionary = ctx.at("dictionary");
    const auto& key = ctx.at("key");
    if (dictionary.isObject()) {
      checkLogic(key.isString(),
                 "Contains: dictionary is an object, key is not a string");
      return dictionary.find(key) != dictionary.items().end();
    } else if (dictionary.isArray()) {
      for (const auto& it : dictionary) {
        if (it == key) {
          return true;
        }
      }
      return false;
    } else if (dictionary.isString()) {
      checkLogic(key.isString(),
                 "Contains: dictionary is a string, key is not a string");
      return dictionary.stringPiece().find(key.stringPiece()) != string::npos;
    } else {
      throw std::logic_error("Contains: dictionary is not object/array/string");
    }
  }

  /**
   * Returns true if dictionary (object/array/string) is empty
   * Usage: @empty(dictionary)
   */
  static dynamic emptyMacro(Context ctx) {
    const auto& dict = ctx.at("dictionary");
    checkLogic(dict.isObject() || dict.isArray() || dict.isString(),
               "empty: dictionary is not object/array/string");
    return dict.empty();
  }

  /**
   * Returns true if A == B
   * Usage: @equals(A,B)
   */
  static dynamic equalsMacro(Context ctx) {
    return ctx.at("A") == ctx.at("B");
  }

  /**
   * Returns true if value is an array
   * Usage: @isArray(value)
   */
  static dynamic isArrayMacro(Context ctx) {
    return ctx.at("value").isArray();
  }

  /**
   * Returns true if value is boolean
   * Usage: @isBool(value)
   */
  static dynamic isBoolMacro(Context ctx) {
    return ctx.at("value").isBool();
  }

  /**
   * Returns true if value is an integer
   * Usage: @isInt(value)
   */
  static dynamic isIntMacro(Context ctx) {
    return ctx.at("value").isInt();
  }

  /**
   * Returns true if value is an object
   * Usage: @isObject(value)
   */
  static dynamic isObjectMacro(Context ctx) {
    return ctx.at("value").isObject();
  }

  /**
   * Returns true if value is a string
   * Usage: @isString(value)
   */
  static dynamic isStringMacro(Context ctx) {
    return ctx.at("value").isString();
  }

  /**
   * Returns true if A < B
   * Usage: @less(A,B)
   */
  static dynamic lessMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(!A.isObject() && !B.isObject(), "Can not compare objects");
    return A < B;
  }

  /**
   * Returns true if A && B. A and B should be booleans.
   * Usage: @and(A,B)
   */
  static dynamic andMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isBool(), "and: A is not bool");
    checkLogic(B.isBool(), "and: B is not bool");
    return A.getBool() && B.getBool();
  }

  /**
   * Returns true if A || B. A and B should be booleans.
   * Usage: @or(A,B)
   */
  static dynamic orMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isBool(), "or: A is not bool");
    checkLogic(B.isBool(), "or: B is not bool");
    return A.getBool() || B.getBool();
  }

  /**
   * Returns true if !A. A should be boolean.
   * Usage: @not(A)
   */
  static dynamic notMacro(Context ctx) {
    const auto& A = ctx.at("A");
    checkLogic(A.isBool(), "not: A is not bool");
    return !A.getBool();
  }

  /**
   * Returns size of object/array/string.
   * Usage: @size(dictionary)
   */
  static dynamic sizeMacro(Context ctx) {
    const auto& dict = ctx.at("dictionary");
    checkLogic(dict.isObject() || dict.isArray() || dict.isString(),
               "size: dictionary is not object/array/string");
    return dict.size();
  }

  /**
   * Adds two integers.
   * Usage: @add(A,B)
   */
  static dynamic addMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isInt(), "add: A is not an integer");
    checkLogic(B.isInt(), "add: B is not an integer");
    return A.getInt() + B.getInt();
  }

  /**
   * Subtracts two integers.
   * Usage: @sub(A,B)
   */
  static dynamic subMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isInt(), "sub: A is not an integer");
    checkLogic(B.isInt(), "sub: B is not an integer");
    return A.getInt() - B.getInt();
  }

  /**
   * Multiplies two integers.
   * Usage: @mul(A,B)
   */
  static dynamic mulMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isInt(), "mul: A is not an integer");
    checkLogic(B.isInt(), "mul: B is not an integer");
    return A.getInt() * B.getInt();
  }

  /**
   * Divides two integers.
   * Usage: @div(A,B)
   */
  static dynamic divMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isInt(), "div: A is not an integer");
    checkLogic(B.isInt(), "div: B is not an integer");
    checkLogic(B.getInt() != 0, "div: B == 0");
    return A.getInt() / B.getInt();
  }

  /**
   * A % B.
   * Usage: @mod(A,B)
   */
  static dynamic modMacro(Context ctx) {
    const auto& A = ctx.at("A");
    const auto& B = ctx.at("B");
    checkLogic(A.isInt(), "mod: A is not an integer");
    checkLogic(B.isInt(), "mod: B is not an integer");
    checkLogic(B.getInt() != 0, "mod: B == 0");
    return A.getInt() % B.getInt();
  }

  /**
   * Throw an exception with custom message
   * Usage: @fail(Your message here)
   */
  static dynamic failMacro(Context ctx) {
    const auto& A = ctx.at("msg");
    checkLogic(A.isString(), "fail: msg is not a string");
    throw std::logic_error(A.data());
  }

  /**
   * Sort an array of strings/integers
   * Usage: @sort(array)
   */
  static dynamic sortMacro(Context ctx) {
    auto dict = std::move(ctx.at("dictionary"));
    checkLogic(dict.isArray(), "sort: dictionary is not an array");
    vector<dynamic> v;
    v.reserve(dict.size());
    for (size_t i = 0; i < dict.size(); ++i) {
      v.push_back(std::move(dict[i]));
    }
    std::sort(v.begin(), v.end());
    for (size_t i = 0; i < v.size(); ++i) {
      dict[i] = std::move(v[i]);
    }
    return dict;
  }

  /**
   * Special built-in that prevents expanding 'macroDef' and 'constDef' objects
   * unless we parse them. For internal use only, nobody should call it
   * explicitly.
   */
  static dynamic noop(const dynamic& json, const Context& ctx) {
    return json;
  }

  /**
   * Tranforms keys and items of object or elements of list.
   * Usage:
   * "type": "transform",
   * "dictionary": obj,
   * "itemTransform": macro with extended context (optional)
   * "keyTranform": macro with extended context (optional)
   * "itemName": string (optional, default: item)
   * "keyName": string (optional, default: key)
   * or
   * "type": "transform",
   * "dictionary": list,
   * "itemTranform": macro with extended context (required)
   * "keyName": string (optional, default: key)
   * "itemName": string (optional, default: item)
   *
   * Extended context is current context with two additional parameters:
   * %keyName% (key of current entry) and %itemName% (value of current entry)
   */
  static dynamic transform(ConfigPreprocessor* p,
                           dynamic json,
                           const Context& ctx) {
    auto dictionary =
      p->expandMacros(moveGet(json, "dictionary", "Transform"), ctx);

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Transform: dictionary is not array/object");

    if (dictionary.empty()) {
      return dictionary;
    }

    auto itemTransform = json.find("itemTransform");
    auto itemName = json.find("itemName");
    string itemNameStr = itemName == json.items().end()
      ? "item" : asString(itemName->second, "Transform: itemName");
    auto keyName = json.find("keyName");
    string keyNameStr = keyName == json.items().end()
      ? "key" : asString(keyName->second, "Transform: keyName");

    if (dictionary.isObject()) {
      auto keyTransform = json.find("keyTransform");
      auto extContext = ctx;
      dynamic res = dynamic::object();
      for (const auto& it : dictionary.items()) {
        auto& key = const_cast<dynamic&>(it.first);
        auto& value = const_cast<dynamic&>(it.second);
        // add %key% and %item% to current context.
        extContext.erase(keyNameStr);
        extContext.erase(itemNameStr);
        auto keyIt = extContext.emplace(keyNameStr, std::move(key)).first;
        auto valueIt = extContext.emplace(itemNameStr, std::move(value)).first;

        auto nKey = keyTransform == json.items().end()
          ? keyIt->second
          : p->expandMacros(keyTransform->second, extContext);
        checkLogic(nKey.isArray() || nKey.isString(),
                   "Transformed key is not array/string");
        if (nKey.isArray() && nKey.empty()) {
          continue;
        }
        auto nItem = itemTransform == json.items().end()
          ? std::move(valueIt->second)
          : p->expandMacros(itemTransform->second, extContext);
        if (nKey.isString()) {
          res.insert(std::move(nKey), std::move(nItem));
        } else { // array
          for (auto& keyIt : nKey) {
            checkLogic(keyIt.isString(),
                       "Transformed key list item is not a string");
            res.insert(std::move(keyIt), nItem);
          }
        }
      }
      return res;
    } else { // array
      checkLogic(itemTransform != json.items().end(),
                 "Transform: itemTransform is required for array");
      auto extContext = ctx;
      for (size_t index = 0; index < dictionary.size(); ++index) {
        auto& item = dictionary[index];
        // add %key% and %item% to current context.
        extContext.erase(keyNameStr);
        extContext.erase(itemNameStr);
        extContext.emplace(keyNameStr, index);
        extContext.emplace(itemNameStr, std::move(item));
        item = p->expandMacros(itemTransform->second, extContext);
      }
      return dictionary;
    }
  }

  /**
   * Iterates over object or array and transforms "value". Literally:
   *
   *   value = initialValue
   *   for (auto& it : dictionary) {
   *     value = transform(it.first, it.second, value)
   *   }
   *   return value
   *
   * Usage:
   * "type": "process",
   * "initialValue": any value,
   * "transform": macro with extended context
   * "keyName": string (optional, default: key)
   * "itemName": string (optional, default: item)
   * "valueName": string (optional, default: value)
   *
   * Extended context is current context with three additional parameters:
   * %keyName% (key of current entry), %itemName% (value of current entry)
   * and %valueName% - current value.
   */
  static dynamic process(ConfigPreprocessor* p,
                         dynamic json,
                         const Context& ctx) {
    auto dictionary =
      p->expandMacros(moveGet(json, "dictionary", "Process"), ctx);
    auto value = p->expandMacros(moveGet(json, "initialValue", "Process"), ctx);
    const auto& transform = tryGet(json, "transform", "Process");

    checkLogic(dictionary.isObject() || dictionary.isArray(),
               "Process: dictionary is not array/object");

    auto itemName = json.find("itemName");
    string itemNameStr = itemName == json.items().end()
      ? "item" : asString(itemName->second, "Process: itemName");
    auto keyName = json.find("keyName");
    string keyNameStr = keyName == json.items().end()
      ? "key" : asString(keyName->second, "Process: keyName");
    auto valueName = json.find("valueName");
    string valueNameStr = valueName == json.items().end()
      ? "value" : asString(valueName->second, "Process: valueName");

    if (dictionary.isObject()) {
      auto extContext = ctx;
      for (auto& item : dictionary.items()) {
        auto& key = const_cast<dynamic&>(item.first);
        auto& val = const_cast<dynamic&>(item.second);
        // add %key%, %item% and %value% to current context.
        extContext.erase(keyNameStr);
        extContext.erase(itemNameStr);
        extContext.erase(valueNameStr);
        extContext.emplace(keyNameStr, std::move(key));
        extContext.emplace(itemNameStr, std::move(val));
        extContext.emplace(valueNameStr, std::move(value));
        value = p->expandMacros(transform, extContext);
      }
      return value;
    } else { // array
      auto extContext = ctx;
      for (size_t index = 0; index < dictionary.size(); ++index) {
        auto& item = dictionary[index];
        // add %key%, %item% and %value% to current context.
        extContext.erase(keyNameStr);
        extContext.erase(itemNameStr);
        extContext.erase(valueNameStr);
        extContext.emplace(keyNameStr, index);
        extContext.emplace(itemNameStr, std::move(item));
        extContext.emplace(valueNameStr, std::move(value));
        value = p->expandMacros(transform, extContext);
      }
      return value;
    }
  }

  /**
   * foreach (key, item) from <from> where <where> use <use> top <int>
   * for top <top> items from dictionary "from" which satisfy "where"
   * condition merge <use> expansions into one dictionary.
   *
   * Usage:
   *  "type": "foreach",
   *  "key": string (optional, default: key)
   *  "item": string (optional, default: item)
   *  "from": object or list
   *  "where": macro with extended context (optional, %key% and %item%)
   *  "use": macro with extended context (optional, %key% and %item%)
   *  "top": int (optional)
   *  "noMatchResult": any value (optional)
   *
   * Example:
   * filter dictionary:
   *  "type": "foreach",
   *  "from": <dictionary>,
   *  "where": <condition>
   *
   * Convert object to list:
   *  "type": "foreach",
   *  "from": <object>,
   *  "use": [ <list item> ]
   *  "noMatchResult": []
   *
   * Grab at most 2 items from <dictionary> that satisfy <condition>:
   *  "type": "foreach",
   *  "from": <dictionary>
   *  "where": <condition>
   *  "top": 2
   */
  static dynamic foreach(ConfigPreprocessor* p,
                         dynamic json,
                         const Context& ctx) {
    auto from = p->expandMacros(moveGet(json, "from", "Foreach"), ctx);
    checkLogic(from.isObject() || from.isArray(),
               "Foreach: from is not object/array");
    auto itemIt = json.find("item");
    string itemStr = itemIt == json.items().end()
      ? "item" : asString(itemIt->second, "Foreach: item");
    auto keyIt = json.find("key");
    string keyStr = keyIt == json.items().end()
      ? "key" : asString(keyIt->second, "Foreach: key");
    size_t top = from.size();
    if (json.find("top") != json.items().end()) {
      auto jtop = p->expandMacros(moveGet(json, "top", "Foreach"), ctx);
      checkLogic(jtop.isInt() && jtop.getInt() >= 0,
                 "Foreach: top should be a non-negative integer");
      top = jtop.getInt();
    }
    auto useIt = json.find("use");
    auto whereIt = json.find("where");

    dynamic result = nullptr;
    auto extContext = ctx;

    auto appendUseToResult = [&]() {
      auto use = p->expandMacros(useIt->second, extContext);
      if (result.isNull()) {
        checkLogic(use.isObject() || use.isArray(),
                   "Foreach: expanded item is not object/array");
        result = std::move(use);
      } else {
        if (result.isObject()) {
          checkLogic(use.isObject(), "Foreach: expanded item is not an object");
          for (auto& it : use.items()) {
            auto& key = const_cast<dynamic&>(it.first);
            auto& value = const_cast<dynamic&>(it.second);
            result.insert(std::move(key), std::move(value));
          }
        } else if (result.isArray()) {
          checkLogic(use.isArray(), "Foreach: expanded item is not an array");
          for (size_t i = 0; i < use.size(); ++i) {
            result.push_back(std::move(use[i]));
          }
        }
      }
    };

    auto satisfiesWhere = [&]() {
      if (whereIt == json.items().end()) {
        return true;
      }
      auto where = p->expandMacros(whereIt->second, extContext);
      checkLogic(where.isBool(), "Foreach: expanded 'where' is not boolean");
      return where.getBool();
    };

    if (from.isArray()) {
      for (size_t i = 0; i < from.size() && top > 0; ++i) {
        extContext.erase(keyStr);
        extContext.erase(itemStr);
        extContext.emplace(keyStr, i);
        auto itemIt = extContext.emplace(itemStr, std::move(from[i])).first;
        if (!satisfiesWhere()) {
          continue;
        }

        if (useIt == json.items().end()) {
          if (result.isNull()) {
            // array of one element
            result = std::initializer_list<dynamic>{
              std::move(itemIt->second)
            };
          } else {
            result.push_back(std::move(itemIt->second));
          }
        } else {
          appendUseToResult();
        }
        --top;
      }
    } else { // object
      for (auto& curIt : from.items()) {
        if (top == 0) {
          break;
        }
        auto& curKey = const_cast<dynamic&>(curIt.first);
        auto& curItem = const_cast<dynamic&>(curIt.second);

        extContext.erase(keyStr);
        extContext.erase(itemStr);
        auto keyIt = extContext.emplace(keyStr, std::move(curKey)).first;
        auto itemIt = extContext.emplace(itemStr, std::move(curItem)).first;
        if (!satisfiesWhere()) {
          continue;
        }

        if (useIt == json.items().end()) {
          if (result.isNull()) {
            result = dynamic::object(std::move(keyIt->second),
                                     std::move(itemIt->second));
          } else {
            result.insert(std::move(keyIt->second), std::move(itemIt->second));
          }
        } else {
          appendUseToResult();
        }
        --top;
      }
    }
    if (result.isNull()) {
      if (auto jnoMatchResult = json.get_ptr("noMatchResult")) {
        return p->expandMacros(std::move(*jnoMatchResult), ctx);
      }
      return from.isObject()
        ? dynamic::object()
        : dynamic(std::initializer_list<dynamic>{});
    }
    return result;
  }

  /**
   * Add values to the context
   * Usage:
   *  "type": "define",
   *  "vars": { "A": "B" },
   *  "result": "%A%"
   * => "B"
   */
  static dynamic define(ConfigPreprocessor* p,
                        dynamic json,
                        const Context& ctx) {
    auto vars = p->expandMacros(moveGet(json, "vars", "Define"), ctx);
    checkLogic(vars.isObject(), "Define: vars is not an object");
    auto& mutContext = const_cast<Context&>(ctx);
    std::vector<Context::iterator> added;
    added.reserve(vars.size());
    for (const auto& it : vars.items()) {
      auto name = asString(it.first, "Define: vars key");
      checkLogic(ctx.find(name) == ctx.end(),
                 "Define: '{}' already exists", name);
      auto value = const_cast<dynamic&>(it.second);
      added.push_back(mutContext.emplace(std::move(name),
                                         std::move(value)).first);
    }
    auto result = p->expandMacros(moveGet(json, "result", "Define"), ctx);
    for (const auto& it : added) {
      mutContext.erase(it);
    }
    return result;
  }

  /**
   * If condition is true, returns "is_true", otherwise "is_false"
   * Usage:
   * "type": "if",
   * "condition": bool,
   * "is_true": any object
   * "is_false": any object
   */
  static dynamic if_(ConfigPreprocessor* p,
                     dynamic json,
                     const Context& ctx) {
    auto condition = p->expandMacros(moveGet(json, "condition", "If"), ctx);
    checkLogic(condition.isBool(), "If: condition is not bool");
    if (condition.getBool()) {
      try {
        return p->expandMacros(moveGet(json, "is_true", "If"), ctx);
      } catch (const std::logic_error& e) {
        throw std::logic_error(string("If 'is_true':\n") + e.what());
      }
    } else {
      try {
        return p->expandMacros(moveGet(json, "is_false", "If"), ctx);
      } catch (const std::logic_error& e) {
        throw std::logic_error(string("If 'is_false':\n") + e.what());
      }
    }
  }
};

///////////////////////////////ConfigPreprocessor///////////////////////////////

ConfigPreprocessor::ConfigPreprocessor(ImportResolverIf& importResolver,
                                       Context globals,
                                       size_t nestedLimit)
  : nestedLimit_(nestedLimit) {

  for (auto& it : globals) {
    auto constObj =
        folly::make_unique<Const>(*this, it.first, std::move(it.second));
    consts_.emplace(std::move(it.first), std::move(constObj));
  }

  addBuiltInMacro("import", { "path" },
    std::bind(&BuiltIns::importMacro, this, std::ref(importResolver), _1));

  addBuiltInMacro("int", { "value" }, &BuiltIns::intMacro);

  addBuiltInMacro("str", { "value" }, &BuiltIns::strMacro);

  addBuiltInMacro("bool", { "value" }, &BuiltIns::boolMacro);

  addBuiltInMacro("keys", { "dictionary" }, &BuiltIns::keysMacro);

  addBuiltInMacro("values", { "dictionary" }, &BuiltIns::valuesMacro);

  addBuiltInMacro("merge", { "params" }, &BuiltIns::mergeMacro);

  addBuiltInMacro("select", { "dictionary", "key" }, &BuiltIns::selectMacro);

  addBuiltInMacro("shuffle", { "dictionary" }, &BuiltIns::shuffleMacro);

  addBuiltInMacro("slice", { "dictionary", "from", "to" },
                  &BuiltIns::sliceMacro);

  addBuiltInMacro("range", { "from", "to" }, &BuiltIns::rangeMacro);

  addBuiltInMacro("contains", { "dictionary", "key" },
                  &BuiltIns::containsMacro);

  addBuiltInMacro("empty", { "dictionary" }, &BuiltIns::emptyMacro);

  addBuiltInMacro("equals", { "A", "B" }, &BuiltIns::equalsMacro);

  addBuiltInMacro("isArray", { "value" }, &BuiltIns::isArrayMacro);

  addBuiltInMacro("isBool", { "value" }, &BuiltIns::isBoolMacro);

  addBuiltInMacro("isInt", { "value" }, &BuiltIns::isIntMacro);

  addBuiltInMacro("isObject", { "value" }, &BuiltIns::isObjectMacro);

  addBuiltInMacro("isString", { "value" }, &BuiltIns::isStringMacro);

  addBuiltInMacro("less", { "A", "B" }, &BuiltIns::lessMacro);

  addBuiltInMacro("and", { "A", "B" }, &BuiltIns::andMacro);

  addBuiltInMacro("or", { "A", "B" }, &BuiltIns::orMacro);

  addBuiltInMacro("not", { "A" }, &BuiltIns::notMacro);

  addBuiltInMacro("size", { "dictionary" }, &BuiltIns::sizeMacro);

  addBuiltInMacro("add", { "A", "B" }, &BuiltIns::addMacro);

  addBuiltInMacro("sub", { "A", "B" }, &BuiltIns::subMacro);

  addBuiltInMacro("mul", { "A", "B" }, &BuiltIns::mulMacro);

  addBuiltInMacro("div", { "A", "B" }, &BuiltIns::divMacro);

  addBuiltInMacro("mod", { "A", "B" }, &BuiltIns::modMacro);

  addBuiltInMacro("fail", { "msg" }, &BuiltIns::failMacro);

  addBuiltInMacro("sort", { "dictionary" }, &BuiltIns::sortMacro);

  addBuiltInMacro("set", { "dictionary", "key", "value" }, &BuiltIns::setMacro);

  builtInCalls_.emplace("macroDef", &BuiltIns::noop);

  builtInCalls_.emplace("constDef", &BuiltIns::noop);

  builtInCalls_.emplace("transform",
    std::bind(&BuiltIns::transform, this, _1, _2));

  builtInCalls_.emplace("process", std::bind(&BuiltIns::process, this, _1, _2));

  builtInCalls_.emplace("if", std::bind(&BuiltIns::if_, this, _1, _2));

  builtInCalls_.emplace("foreach", std::bind(&BuiltIns::foreach, this, _1, _2));

  builtInCalls_.emplace("define", std::bind(&BuiltIns::define, this, _1, _2));
}

void ConfigPreprocessor::addBuiltInMacro(string name, vector<dynamic> params,
                                         Macro::Func func) {
  macros_.emplace(
      name,
      folly::make_unique<Macro>(name, std::move(params), std::move(func)));
}

dynamic ConfigPreprocessor::replaceParams(StringPiece str,
                                          const Context& context) const {
  string buf;
  while (!str.empty()) {
    auto pos = unescapeUntil(str, buf, '%');
    if (pos == string::npos) {
      break;
    }
    str = str.subpiece(pos + 1);

    string paramName;
    auto nextPos = unescapeUntil(str, paramName, '%');
    checkLogic(nextPos != string::npos,
               "Odd number of percent signs in string around '{}'", str);

    // first check current context, then global context
    auto paramIt = context.find(paramName);
    const auto& substitution = paramIt == context.end()
      ? tryGet(consts_, paramName, "Param in string")->getResult()
      : paramIt->second;

    if (buf.empty() && nextPos == str.size() - 1) {
      // whole string is a parameter. May be substituted to any value.
      return substitution;
    } else {
      // param inside string e.g. a%param%b
      checkLogic(substitution.isString(),
                 "Param in string '{}' is not a string", paramName);
      auto sp = substitution.stringPiece();
      buf.append(sp.begin(), sp.end());
    }
    str = str.subpiece(nextPos + 1);
  }
  return buf;
}

vector<dynamic>
ConfigPreprocessor::getCallParams(StringPiece str,
                                  const Context& context) const {
  // all params are substituted. But inner macro calls are possible.
  vector<dynamic> result;
  while (true) {
    if (str.empty()) {
      // it is one parameter - empty string e.g. @a() is call with one parameter
      result.emplace_back("");
      break;
    }

    auto commaPos = findUnescaped(str, ',');
    if (commaPos == string::npos) {
      // no commas - only one param;
      result.push_back(expandStringMacro(trim(str), context));
      break;
    }

    auto firstOpened = findUnescaped(str.subpiece(0, commaPos), '(');
    // macro call with params
    if (firstOpened != string::npos) {
      // first param is inner macro with params
      auto closing = matchingUnescapedBracket(str, firstOpened);
      checkLogic(closing != string::npos, "Brackets do not match: {}", str);

      if (closing == str.size() - 1) {
        // no more params, only single inner macro
        result.push_back(expandStringMacro(trim(str), context));
        break;
      }

      commaPos = closing + 1;
      while (commaPos < str.size() && str[commaPos] != ',') {
        ++commaPos;
      }
      checkLogic(commaPos != str.size(), "No comma after closing bracket");
    }

    // add first param
    auto firstParam = str.subpiece(0, commaPos);
    result.push_back(expandStringMacro(trim(firstParam), context));
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
    checkLogic(str[str.size() - 1] == ')',
               "No closing bracket for call '{}'", str);

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
  try {
    return inner->getResult(inner->getContext(std::move(innerParams)));
  } catch (const std::logic_error& e) {
    throw std::logic_error("Macro in string '" + nameStr + "':\n" + e.what());
  }
}

dynamic ConfigPreprocessor::expandMacros(dynamic json,
                                         const Context& context) const {
  NestedLimitGuard nestedGuard(nestedLimit_);

  if (json.isString()) {
    // look for macros in string
    return expandStringMacro(json.stringPiece(), context);
  } else if (json.isObject()) {
    // check for built-in calls and long-form macros
    auto typeIt = json.find("type");
    if (typeIt != json.items().end()) {
      auto type = expandMacros(typeIt->second, context);
      if (type.isString()) {
        auto typeStr = type.stringPiece().str();
        // built-in call
        auto builtInIt = builtInCalls_.find(typeStr);
        if (builtInIt != builtInCalls_.end()) {
          try {
            return builtInIt->second(std::move(json), context);
          } catch (const std::logic_error& e) {
            throw std::logic_error("Built-in '" + typeStr + "':\n" + e.what());
          }
        }
        // long form macro substitution
        auto macroIt = macros_.find(typeStr);
        if (macroIt != macros_.end()) {
          const auto& inner = macroIt->second;
          auto innerContext = inner->getContext(std::move(json));
          for (auto& it : innerContext) {
            try {
              it.second = expandMacros(std::move(it.second), context);
            } catch (const std::logic_error& e) {
              throw std::logic_error("Macro '" + typeStr +
                "', param '" + it.first + "':\n" + e.what());
            }
          }
          try {
            return inner->getResult(std::move(innerContext));
          } catch (const std::logic_error& e) {
            throw std::logic_error("Macro '" + typeStr + "':\n" + e.what());
          }
        }
      }
    }

    // raw object
    dynamic result = dynamic::object();
    for (const auto& it : json.items()) {
      auto& value = const_cast<dynamic&>(it.second);
      try {
        auto nKey = expandMacros(it.first, context);
        checkLogic(nKey.isString(), "Expanded key is not a string");
        result.insert(std::move(nKey),
                      expandMacros(std::move(value), context));
      } catch (const std::logic_error& e) {
        throw std::logic_error(string("Raw object property '") +
          it.first.stringPiece().str() + "':\n" + e.what());
      }
    }
    return result;
  } else if (json.isArray()) {
    for (size_t i = 0; i < json.size(); ++i) {
      auto& value = json[i];
      try {
        value = expandMacros(std::move(value), context);
      } catch (const std::logic_error& e) {
        throw std::logic_error("Array element #" + folly::to<string>(i) +
          ":\n" + e.what());
      }
    }
    return json;
  } else {
    // some number or other type of json. Return 'as is'.
    return json;
  }
}

void ConfigPreprocessor::parseConstDefs(dynamic jconsts) {
  auto consts = expandMacros(std::move(jconsts), emptyContext_);
  checkLogic(consts.isArray(), "config consts is not an array");
  for (const auto& it : consts) {
    checkLogic(it.isObject(), "constDef is not an object");
    auto type = asString(tryGet(it, "type", "constDef"), "constDef type");
    checkLogic(type == "constDef", "constDef has invalid type: {}", type);
    auto name = asString(tryGet(it, "name", "constDef"), "constDef name");
    const auto& result = tryGet(it, "result", "constDef");
    try {
      consts_.emplace(name, folly::make_unique<Const>(*this, name, result));
    } catch (const std::logic_error& e) {
      throw std::logic_error("'" + name + "' const:\n" + e.what());
    }
  }
}

void ConfigPreprocessor::parseMacroDef(const dynamic& jkey,
                                       const dynamic& obj) {
  auto key = asString(jkey, "macro definition key");
  checkLogic(obj.isObject(), "'{}' macro definition is not an object", key);
  auto objType = asString(tryGet(obj, "type", "macro definition"),
                          "macro definition type");

  if (objType == "macroDef") {
    const auto& res = tryGet(obj, "result", "Macro definition");
    vector<dynamic> params;
    auto paramsIt = obj.find("params");
    if (paramsIt != obj.items().end()) {
      checkLogic(paramsIt->second.isArray(),
                 "'{}' macroDef params is not an array", key);
      for (auto& paramObj : paramsIt->second) {
        params.push_back(paramObj);
      }
    }
    auto f = [res, this](const Context& ctx) {
      return expandMacros(res, ctx);
    };
    macros_.emplace(key, folly::make_unique<Macro>(key, params, std::move(f)));
  } else if (objType == "constDef") {
    checkLogic(obj.isObject(), "constDef is not an object");
    const auto& result = tryGet(obj, "result", "constDef");
    consts_.emplace(key, folly::make_unique<Const>(*this, key, result));
  } else {
    throw std::logic_error("Unknown macro definition type: " + objType);
  }
}

void ConfigPreprocessor::parseMacroDefs(dynamic jmacros) {
  auto macros = expandMacros(std::move(jmacros), emptyContext_);
  checkLogic(macros.isObject() || macros.isArray(),
             "config macros is not an array/object");

  if (macros.isObject()) {
    for (const auto& it : macros.items()) {
      parseMacroDef(it.first, it.second);
    }
  } else { // array
    for (const auto& it : macros) {
      parseMacroDefs(it);
    }
  }
}

dynamic ConfigPreprocessor::getConfigWithoutMacros(
    StringPiece jsonC,
    ImportResolverIf& importResolver,
    std::unordered_map<string, dynamic> globalParams,
    size_t nestedLimit) {

  auto config = parseJsonString(stripComments(jsonC));
  checkLogic(config.isObject(), "config is not an object");

  ConfigPreprocessor prep(importResolver, std::move(globalParams), nestedLimit);

  // parse and add consts. DEPRECATED.
  auto jconsts = config.get_ptr("consts");
  if (jconsts) {
    prep.parseConstDefs(std::move(*jconsts));
    config.erase("consts");
  }

  // parse and add macros
  auto jmacros = config.get_ptr("macros");
  if (jmacros) {
    prep.parseMacroDefs(std::move(*jmacros));
    config.erase("macros");
  }

  return prep.expandMacros(std::move(config), emptyContext_);
}

}}  // facebook::memcache
