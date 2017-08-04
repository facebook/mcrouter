/*
 *  Copyright (c) 2017, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <cctype>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>
#include <utility>

#include <folly/Conv.h>
#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/json.h>

#include "mcrouter/lib/carbon/Fields.h"
#include "mcrouter/lib/carbon/Keys.h"
#include "mcrouter/tools/mcpiper/PrettyFormat.h"
#include "mcrouter/tools/mcpiper/StyledString.h"
#include "mcrouter/tools/mcpiper/ValueFormatter.h"

namespace carbon {
namespace detail {

class McPiperVisitor {
 private:
  const std::unordered_set<std::string> kExcuseValues = {"value",
                                                         "flags",
                                                         "result",
                                                         "key"};

 public:
  explicit McPiperVisitor(bool script)
      : script_(script), nested(script_ ? 1 : 0) {}

  template <class T>
  bool enterMixin(size_t /* id */, folly::StringPiece /* name */, const T&) {
    return true;
  }

  bool leaveMixin() {
    return true;
  }

  template <class T>
  bool visitField(size_t /* id */, folly::StringPiece name, const T& t) {
    if (kExcuseValues.find(name.str()) == kExcuseValues.end()) {
      render(name, t);
    }
    return true;
  }

  facebook::memcache::StyledString styled() && {
    return std::move(out_);
  }

  void setNested(uint32_t newNested) {
    nested = newNested;
  }

 private:
  const bool script_;
  facebook::memcache::StyledString out_;
  const facebook::memcache::PrettyFormat format_{};
  uint32_t nested{0};

  std::string getSpaces() const {
    return std::string(nested, ' ');
  }

  void renderHeader(folly::StringPiece name) {
    if (!name.empty()) {
      auto s = getSpaces();
      if (script_) {
        if (!out_.empty()) {
          out_.append(",");
        }
        out_.append("\n  ");
        out_.append(getSpaces());
        out_.append("\"");
        out_.append(name.str());
        out_.append("\": ");
      } else {
        out_.append("\n  ");
        out_.append(getSpaces());
        out_.append(name.str(), format_.msgAttrColor);
        out_.append(": ", format_.msgAttrColor);
      }
    }
  }

  void renderStringField(folly::StringPiece name, const std::string& str) {
    if (!script_ && str.empty()) {
      return;
    }
    renderHeader(name);
    if (script_) {
      out_.append("\"");
      if (!std::all_of<std::string::const_iterator, int(int)>(
              str.begin(), str.end(), std::isprint)) {
        /* JSON doesn't deal with arbitrary binary data - the input string
           must be valid UTF-8.  So we just hex encode the whole string. */
        out_.append(folly::hexlify(str));
      } else {
        out_.append(folly::cEscape<std::string>(str));
      }
      out_.append("\"");
    } else {
      out_.append(folly::backslashify(str), format_.dataValueColor);
    }
  }

  template <class T>
  std::enable_if_t<!carbon::IsCarbonStruct<T>::value> render(
      folly::StringPiece name,
      const T& /* t */) {
    renderHeader(name);
    out_.append("[Unserializable]", format_.dataValueColor);
  }

  template <class T>
  std::enable_if_t<
      carbon::IsCarbonStruct<T>::value &&
      !carbon::IsThriftWrapperStruct<T>::value>
  render(folly::StringPiece name, const T& t) {
    McPiperVisitor printer(script_);
    renderHeader(name);
    nested += 1;
    printer.setNested(nested);
    nested -= 1;
    t.visitFields(printer);
    auto str = std::move(printer).styled();
    out_.pushBack('{');
    out_.append(str);
    out_.append("\n  ");
    out_.append(getSpaces());
    out_.pushBack('}');
  }

  template <class T>
  std::enable_if_t<
      carbon::IsCarbonStruct<T>::value &&
      carbon::IsThriftWrapperStruct<T>::value>
  render(folly::StringPiece name, const T& /* unused */) {
    renderHeader(name);
    out_.append("[Thrift structure]");
  }

  template <class BinaryType>
  void render(folly::StringPiece name, const carbon::Keys<BinaryType>& keys) {
    renderHeader(name);
    out_.append(keys.routingKey().str(), format_.dataValueColor);
  }

  template <class T>
  void render(folly::StringPiece name, const folly::Optional<T>& t) {
    if (t) {
      render(name, *t);
    }
  }

  template <class T>
  void render(folly::StringPiece name, const std::vector<T>& ts) {
    renderHeader(name);
    out_.pushBack('[');

    bool firstInArray = true;
    for (const auto& t : ts) {
      if (!firstInArray) {
        out_.pushBack(',');
      }
      firstInArray = false;
      McPiperVisitor printer(script_);
      printer.render("", t);
      out_.append(std::move(printer).styled());
    }
    out_.pushBack(']');
  }
};

template <>
inline void McPiperVisitor::render(
    folly::StringPiece name,
    const folly::IOBuf& buf) {
  auto buffer = buf;
  auto strPiece = folly::StringPiece(buffer.coalesce());
  renderStringField(name, strPiece.str());
}

template <>
inline void McPiperVisitor::render(
    folly::StringPiece name,
    const std::string& str) {
  renderStringField(name, str);
}

template <>
inline void McPiperVisitor::render(folly::StringPiece name, const bool& b) {
  renderHeader(name);
  out_.append(b ? "true" : "false", format_.dataValueColor);
}
} // detail

template <class R>
facebook::memcache::StyledString
print(const R& req, folly::StringPiece /* name */, bool script) {
  detail::McPiperVisitor printer(script);
  req.visitFields(printer);
  return std::move(printer).styled();
}
} // carbon
