/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <folly/dynamic.h>

namespace facebook { namespace memcache {

class ImportResolverIf;

/**
 * Removes comments from JSONC (JSON with Comments) and expands macros
 * inside this JSON.
 */
class ConfigPreprocessor {
 public:
  /**
   * Method to expand macros and get resulting dynamic object.
   *
   * @param jsonC JSON with comments and macros
   * @param importResolver resolves @import macros
   * @param globalParams parameters available in all macros. Should not have
   *                     macros.
   * @param nestedLimit maximum number of nested macros/objects.
   *
   * @return JSON without macros
   * @throws std::logic_error/folly::ParseError if jsonC is invalid
   */
  static folly::dynamic getConfigWithoutMacros(
    folly::StringPiece jsonC,
    ImportResolverIf& importResolver,
    std::unordered_map<std::string, folly::dynamic> globalParams,
    size_t nestedLimit = 250);

 private:
  /**
   * Inner representation of macro object
   */
  class Macro;

  /**
   * Inner representation of const/global param
   */
  class Const;

  /**
   * Built-in calls and macros
   */
  class BuiltIns;

  typedef std::unordered_map<std::string, folly::dynamic> Context;

  std::unordered_map<std::string, std::unique_ptr<Macro>> macros_;
  std::unordered_map<std::string, std::unique_ptr<Const>> consts_;
  std::unordered_map<std::string, folly::dynamic> importCache_;
  std::unordered_map<
    std::string,
    std::function<folly::dynamic(const folly::dynamic&, const Context&)>
  > builtInCalls_;

  mutable size_t nestedLimit_;

  static const Context emptyContext_;

  /**
   * Create preprocessor with given macros
   *
   * @param importResolver resolves @import macros
   * @param globals parameters available in all macros. Should not have
   *                macros.
   * @param nestedLimit maximum number of nested macros/objects.
   */
  ConfigPreprocessor(ImportResolverIf& importResolver,
                     Context globals,
                     size_t nestedLimit);

  /**
   * Expands all macros found in json
   *
   * @param json object with macros
   * @param context current context (parameters that should be substituted)
   *
   * @return json object without macros
   */
  folly::dynamic
  expandMacros(folly::dynamic json, const Context& context) const;

  /**
   * Parses parameters passed to macro call inside string like in
   * @a(param1,%substituteMe%)
   *    ^...................^
   *             str
   */
  std::vector<folly::dynamic>
  getCallParams(folly::StringPiece str, const Context& context) const;

  /**
   * Substitute params from context (substrings like %paramName%)
   * with their values
   */
  folly::dynamic
  replaceParams(folly::StringPiece str, const Context& context) const;

  /**
   * Expand macro inside string like in "@macroName(params)"
   *                                     ^................^
   *                                           macro
   */
  folly::dynamic
  expandStringMacro(folly::StringPiece str, const Context& params) const;

  void addBuiltInMacro(std::string name, std::vector<folly::dynamic> params,
                       std::function<folly::dynamic(Context)> func);

  void parseConstDefs(folly::dynamic jconsts);

  void parseMacroDef(const folly::dynamic& key, const folly::dynamic& obj);

  void parseMacroDefs(folly::dynamic jmacros);
};

}}  // facebook::memcache
