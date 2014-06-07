#include "standalone_options.h"

#include "folly/Format.h"
#include "folly/Range.h"

namespace facebook { namespace memcache { namespace mcrouter {
namespace options {

McrouterStandaloneOptions substituteTemplates(McrouterStandaloneOptions opts) {
  folly::StringPiece pidfile = opts.pidfile;
  auto dollar_port = pidfile.find("$port");
  if (dollar_port != std::string::npos) {
    // max port number is 5 chars, so we don't need extra (except the '\0')
    int port = opts.ports.empty() ? 0 : opts.ports[0];
    opts.pidfile = folly::format("{}{}{}",
                                 pidfile.subpiece(0, dollar_port),
                                 port,
                                 pidfile.subpiece(dollar_port + 5)).str();
  }

  return opts;
}

}}}}  // facebook::memcache::mcrouter::options
