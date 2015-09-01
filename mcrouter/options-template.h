/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
class OPTIONS_NAME : public McrouterOptionsBase {
 public:
#define MCROUTER_STRING_MAP std::unordered_map<std::string, std::string>
#define mcrouter_option(_type, _name, _def, _l, _s, _d, _T) _type _name{_def};
#define mcrouter_option_string(_n, _f, _l, _s, _d) \
  mcrouter_option(std::string, _n, _f, _l, _s, _d, string)
#define mcrouter_option_integer(_t, _n, _f, _l, _s, _d) \
  mcrouter_option(_t, _n, _f, _l, _s, _d, integer)
#define mcrouter_option_double(_t, _n, _f, _l, _s, _d) \
  mcrouter_option(_t, _n, _f, _l, _s, _d, double_precision)
#define mcrouter_option_toggle(_n, _f, _l, _s, _d) \
  mcrouter_option(bool, _n, _f, _l, _s, _d, toggle)
#define mcrouter_option_string_map(_n, _l, _s, _d) \
  mcrouter_option(MCROUTER_STRING_MAP, _n, , _l, _s, _d, string_map)
#define mcrouter_option_other(_t, _n, _f, _l, _s, _d) \
  mcrouter_option(_t, _n, _f, _l, _s, _d, other)

  #include OPTIONS_FILE

  OPTIONS_NAME() = default;

  OPTIONS_NAME(OPTIONS_NAME&&) = default;
  OPTIONS_NAME& operator=(OPTIONS_NAME&&) = default;

  OPTIONS_NAME clone() const {
    return *this;
  }

  void forEach(std::function<void(const std::string&,
                                  McrouterOptionData::Type,
                                  const boost::any&)> f) const {

    #undef mcrouter_option
    #define mcrouter_option(_type, _name, _f, _l, _s, _d, _Type)  \
      f(#_name, McrouterOptionData::Type::_Type,                  \
        boost::any(const_cast<_type*>(&_name)));

    #include OPTIONS_FILE
  }

  static std::vector<McrouterOptionData> getOptionData() {
    std::vector<McrouterOptionData> ret;
    std::string current_group;

#undef mcrouter_option
#define mcrouter_option(_type, _name, _default, _lopt, _sopt, _doc, _Type) \
    {                                                                      \
      McrouterOptionData opt;                                              \
      opt.type = McrouterOptionData::Type:: _Type;                         \
      opt.name = #_name;                                                   \
      opt.group = current_group;                                           \
      opt.default_value = #_default;                                       \
      opt.long_option = _lopt;                                             \
      opt.short_option = _sopt;                                            \
      opt.docstring = _doc;                                                \
      ret.push_back(opt);                                                  \
    }

#undef mcrouter_option_group
#define mcrouter_option_group(_name) current_group = _name;

    #include OPTIONS_FILE

    return ret;
  }
 private:
  OPTIONS_NAME(const OPTIONS_NAME&) = default;
  OPTIONS_NAME& operator=(const OPTIONS_NAME&) = default;
};

#undef mcrouter_option_string
#undef mcrouter_option_integer
#undef mcrouter_option_double
#undef mcrouter_option_toggle
#undef mcrouter_option_string_map
#undef mcrouter_option_other
#undef mcrouter_option
#undef MCROUTER_STRING_MAP
