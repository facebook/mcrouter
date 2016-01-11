/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <cstring>
#include <iostream>
#include <signal.h>

#include <glog/logging.h>

#include <boost/program_options.hpp>
#include <boost/regex.hpp>

#include <folly/Format.h>
#include <folly/io/async/EventBase.h>
#include <folly/IPAddress.h>
#include <folly/SocketAddress.h>

#include "mcrouter/lib/mc/msg.h"

#include "mcrouter/tools/mcpiper/AnsiColorCodeStream.h"
#include "mcrouter/tools/mcpiper/Color.h"
#include "mcrouter/tools/mcpiper/Config.h"
#include "mcrouter/tools/mcpiper/FifoReader.h"
#include "mcrouter/tools/mcpiper/ParserMap.h"
#include "mcrouter/tools/mcpiper/PrettyFormat.h"
#include "mcrouter/tools/mcpiper/StyledString.h"
#include "mcrouter/tools/mcpiper/Util.h"
#include "mcrouter/tools/mcpiper/ValueFormatter.h"

using namespace facebook::memcache;

namespace {

struct Settings {
  // Positional args
  std::string matchExpression;

  // Named args
  bool disableColor{false};
  std::string fifoRoot{getDefaultFifoRoot()};
  std::string filenamePattern;
  std::string host;
  bool ignoreCase{false};
  bool invertMatch{false};
  uint32_t maxMessages{0};
  uint32_t numAfterMatch{0};
  uint16_t port{0};
  bool quiet{false};
  std::string timeFormat;
  uint32_t valueMinSize{0};
};

// Constants
const PrettyFormat kFormat{}; // Default constructor = default coloring.

// Globals
AnsiColorCodeStream gTargetOut(std::cout);
Settings gSettings;
std::unique_ptr<boost::regex> gDataPattern;
std::unique_ptr<ValueFormatter> gValueFormatter = createValueFormatter();
uint64_t gTotalMessages{0};
uint64_t gPrintedMessages{0};
uint32_t afterMatchCount{0};
std::function<std::string(const struct timeval& ts)> gPrintTime{nullptr};
struct timeval gPrevTs = {0, 0};
folly::IPAddress gHost;

std::string getUsage(const char* binaryName) {
  return folly::sformat(
      "Usage: {} [OPTION]... [PATTERN]\n"
      "Search for PATTERN in each mcrouter debug fifo in FIFO_ROOT "
      "(see options list) directory.\n"
      "If PATTERN is not provided, match everything.\n"
      "PATTERN is, by default, a basic regular expression (BRE).\n"
      , binaryName);
}

Settings parseOptions(int argc, char **argv) {
  Settings settings;

  namespace po = boost::program_options;

  // Named options
  po::options_description namedOpts("Allowed options");
  namedOpts.add_options()
    ("help,h", "Print this help message.")
    ("disable-color,K",
      po::bool_switch(&settings.disableColor)->default_value(false),
      "Turn off colorized output.")
    ("fifo-root,f",
      po::value<std::string>(&settings.fifoRoot),
      "Path of mcrouter fifo's directory.")
    ("filename-pattern,P",
      po::value<std::string>(&settings.filenamePattern),
      "Basic regular expression (BRE) to match the name of the fifos.")
    ("host,H",
      po::value<std::string>(&settings.host),
      "Show only messages sent/received to provided IP address.")
    ("ignore-case,i",
      po::bool_switch(&settings.ignoreCase)->default_value(false),
      "Ignore case on search patterns")
    ("invert-match,v",
      po::bool_switch(&settings.invertMatch)->default_value(false),
      "Invert match")
    ("max-messages,n",
      po::value<uint32_t>(&settings.maxMessages),
      "Display only <arg> messages and exit.")
    ("num-after-match,A",
      po::value<uint32_t>(&settings.numAfterMatch),
      "Shows <arg> messages after a matched message.")
    ("port,p",
      po::value<uint16_t>(&settings.port),
      "Show only messages transmitted in provided port.")
    ("quiet,q",
      po::bool_switch(&settings.quiet)->default_value(false),
      "Doesn't display values.")
    ("time-format,t",
      po::value<std::string>(&settings.timeFormat),
      "Displays timestamp on every match; "
      "ARG is \"absolute\", \"diff\" or \"offset\".")
    ("value-min-size,m",
      po::value<uint32_t>(&settings.valueMinSize),
      "Minimum size of the value of messages to display")
  ;

  // Positional arguments - hidden from the help message
  po::options_description hiddenOpts("Hidden options");
  hiddenOpts.add_options()
    ("match-expression",
     po::value<std::string>(&settings.matchExpression),
     "Match expression")
  ;
  po::positional_options_description posArgs;
  posArgs.add("match-expression", 1);

  // Parse command line
  po::variables_map vm;
  try {
    // Build all options
    po::options_description allOpts;
    allOpts.add(namedOpts).add(hiddenOpts);

    // Parse
    po::store(po::command_line_parser(argc, argv)
        .options(allOpts).positional(posArgs).run(), vm);
    po::notify(vm);
  } catch (po::error& ex) {
    LOG(ERROR) << ex.what();
    exit(1);
  }

  // Handles help
  if (vm.count("help")) {
    std::cout << getUsage(argv[0]);
    std::cout << std::endl;

    // Print only named options
    namedOpts.print(std::cout);
    exit(0);
  }

  // Handles constraints
  CHECK(!settings.fifoRoot.empty())
    << "Fifo's directory (--fifo-root) cannot be empty";

  return settings;
}

void cleanExit(int32_t status) {
  for (auto sig : {SIGINT, SIGABRT, SIGQUIT, SIGPIPE, SIGWINCH}) {
    signal(sig, SIG_IGN);
  }

  if (status >= 0) {
    gTargetOut << "exit\n"
               << folly::format("{} messages received, {} printed",
                                gTotalMessages, gPrintedMessages)
               << '\n';
  }

  gTargetOut.flush();

  exit(status);
}

/**
 * Matches all the occurences of "pattern" in "text"
 *
 * @return A vector of pairs containing the index and size (respectively)
 *         of all ocurrences.
 */
std::vector<std::pair<size_t, size_t>> matchAll(folly::StringPiece text,
                                                const boost::regex& pattern) {
  std::vector<std::pair<size_t, size_t>> result;

  boost::cregex_token_iterator it(text.begin(), text.end(), pattern);
  boost::cregex_token_iterator end;
  while (it != end) {
    result.emplace_back(it->first - text.begin(), it->length());
    ++it;
  }
  return result;
}

bool matchIPAddress(const folly::IPAddress& expectedIp,
                    const folly::SocketAddress& address) {
  return !address.empty() && expectedIp == address.getIPAddress();
}

bool matchPort(uint16_t expectedPort, const folly::SocketAddress& address) {
  return !address.empty() && expectedPort == address.getPort();
}

std::string serializeMessageHeader(const McMsgRef& msg,
                                   std::string matchingKey) {
  std::string out;

  if (msg->op != mc_op_unknown) {
    out.append(mc_op_to_string(msg->op));
  }
  if (msg->result != mc_res_unknown) {
    if (out.size() > 0) {
      out.push_back(' ');
    }
    out.append(mc_res_to_string(msg->result));
  }
  if (msg->key.len) {
    if (out.size() > 0) {
      out.push_back(' ');
    }
    out.append(folly::backslashify(to<std::string>(msg->key)));
  }
  if (!matchingKey.empty()) {
    out.append(
        " [" + folly::backslashify(matchingKey) + "]");
  }

  return out;
}

std::string serializeAddresses(const folly::SocketAddress& fromAddress,
                               const folly::SocketAddress& toAddress) {
  std::string out;

  if (!fromAddress.empty()) {
    out.append(fromAddress.describe());
  }
  if (!fromAddress.empty() || !toAddress.empty()) {
    out.append(" -> ");
  }
  if (!toAddress.empty()) {
    out.append(toAddress.describe());
  }

  return out;
}

void msgReady(uint64_t reqid, McMsgRef msg, std::string matchingKey,
              const folly::SocketAddress& fromAddress,
              const folly::SocketAddress& toAddress) {
  if (msg->op == mc_op_end) {
    return;
  }

  ++gTotalMessages;

  // Initial filters
  if (!gHost.empty() &&
      !matchIPAddress(gHost, fromAddress) &&
      !matchIPAddress(gHost, toAddress)) {
    return;
  }
  if (gSettings.port != 0 &&
      !matchPort(gSettings.port, fromAddress) &&
      !matchPort(gSettings.port, toAddress)) {
    return;
  }
  if (msg->value.len < gSettings.valueMinSize) {
    return;
  }

  StyledString out;
  out.append("\n");

  if (gPrintTime) {
    timeval ts;
    gettimeofday(&ts, nullptr);
    out.append(gPrintTime(ts));
  }

  out.append(serializeAddresses(fromAddress, toAddress));
  out.append("\n");

  out.append("{\n", kFormat.dataOpColor);

  // Msg header
  auto msgHeader = serializeMessageHeader(msg, std::move(matchingKey));
  if (!msgHeader.empty()) {
    out.append("  ");
    out.append(std::move(msgHeader), kFormat.headerColor);
  }

  // Msg attributes
  out.append("\n  reqid: ", kFormat.msgAttrColor);
  out.append(folly::sformat("0x{:x}", reqid), kFormat.dataValueColor);
  out.append("\n  flags: ", kFormat.msgAttrColor);
  out.append(folly::sformat("0x{:x}", msg->flags), kFormat.dataValueColor);
  if (msg->flags) {
    auto flagDesc = describeFlags(msg->flags);

    if (!flagDesc.empty()) {
      out.pushAppendColor(kFormat.attrColor);
      out.append(" [");
      bool first = true;
      for (auto& s : flagDesc) {
        if (!first) {
          out.append(", ");
        }
        first = false;
        out.append(s);
      }
      out.pushBack(']');
      out.popAppendColor();
    }
  }
  if (msg->exptime) {
      out.append("\n  exptime: ", kFormat.msgAttrColor);
      out.append(folly::format("{:d}", msg->exptime).str(),
                 kFormat.dataValueColor);
  }

  out.pushBack('\n');

  if (msg->value.len) {
    auto value = to<folly::StringPiece>(msg->value);
    size_t uncompressedSize;
    auto formattedValue =
      gValueFormatter->uncompressAndFormat(value,
                                           msg->flags,
                                           kFormat,
                                           uncompressedSize);

    out.append("  value size: ", kFormat.msgAttrColor);
    if (uncompressedSize != value.size()) {
      out.append(
        folly::sformat("{} uncompressed, {} compressed, {:.2f}% savings",
                       uncompressedSize, value.size(),
                       100.0 - 100.0 * value.size()/uncompressedSize),
        kFormat.dataValueColor);
    } else {
      out.append(folly::to<std::string>(value.size()), kFormat.dataValueColor);
    }

    if (!gSettings.quiet) {
      out.append("\n  value: ", kFormat.msgAttrColor);
      out.append(formattedValue);
    }
    out.pushBack('\n');
  }

  out.append("}\n", kFormat.dataOpColor);

  // Match pattern
  if (gDataPattern) {
    auto matches = matchAll(out.text(), *gDataPattern);
    auto success = matches.empty() == gSettings.invertMatch;

    if (!success && afterMatchCount == 0) {
      return;
    }
    if (!gSettings.invertMatch) {
      for (auto& m : matches) {
        out.setFg(m.first, m.second, kFormat.matchColor);
      }
    }

    // Reset after match
    if (success && gSettings.numAfterMatch > 0) {
      afterMatchCount = gSettings.numAfterMatch + 1;
    }
  }

  gTargetOut << out;
  gTargetOut.flush();

  ++gPrintedMessages;

  if (gSettings.maxMessages > 0 && gPrintedMessages >= gSettings.maxMessages) {
    cleanExit(0);
  }

  if (gSettings.numAfterMatch > 0) {
    --afterMatchCount;
  }
}

boost::regex::flag_type getFlags() {
  boost::regex::flag_type flags = boost::regex_constants::basic;
  if (gSettings.ignoreCase) {
    flags |= boost::regex_constants::icase;
  }
  return flags;
}

/**
 * Builds the regex to match fifos' names.
 */
std::unique_ptr<boost::regex> buildFilenameRegex() noexcept {
  if (!gSettings.filenamePattern.empty()) {
    try {
      return folly::make_unique<boost::regex>(gSettings.filenamePattern,
                                              getFlags());
    } catch (const std::exception& e) {
      LOG(ERROR) << "Invalid filename pattern: " << e.what();
      exit(1);
    }
  }
  return nullptr;
}

/**
 * Builds the regex to match data.
 *
 * @returns   The regex or null (to match everything).
 */
std::unique_ptr<boost::regex> buildDataRegex() noexcept {
  if (!gSettings.matchExpression.empty()) {
    try {
      return folly::make_unique<boost::regex>(gSettings.matchExpression,
                                              getFlags());
    } catch (const std::exception& e) {
      LOG(ERROR) << "Invalid pattern: " << e.what();
      exit(1);
    }
  }
  return nullptr;
}

void run() {
  // Builds filename pattern
  auto filenamePattern = buildFilenameRegex();
  if (filenamePattern) {
    std::cout << "Filename pattern: " << *filenamePattern << std::endl;
  }

  // Builds data pattern
  gDataPattern = buildDataRegex();
  if (gDataPattern) {
    if (gSettings.invertMatch) {
      std::cout << "Don't match: ";
    } else {
      std::cout << "Match: ";
    }
    std::cout << *gDataPattern << std::endl;
  }

  // Coloring
  if (gSettings.disableColor) {
    gTargetOut.setColorOutput(false);
  }

  // Host and Port
  if (!gSettings.host.empty()) {
    try {
      gHost = folly::IPAddress(gSettings.host);
      std::cout << "Host: " << gHost.toFullyQualified() << std::endl;
    } catch (...) {
      LOG(ERROR) << "Invalid IP address provided: " << gSettings.host;
      exit(1);
    }
  }
  if (gSettings.port != 0) {
    std::cout << "Port: " << gSettings.port << std::endl;
  }

  // Time Function
  if (!gSettings.timeFormat.empty()) {
    if (gSettings.timeFormat == "absolute") {
      gPrintTime = printTimeAbsolute;
    } else if (gSettings.timeFormat == "diff") {
      gPrintTime = [](const struct timeval& ts) {
        return printTimeDiff(ts, gPrevTs);
      };
      gettimeofday(&gPrevTs, nullptr);
    } else if (gSettings.timeFormat == "offset") {
      gPrintTime = [](const struct timeval& ts) {
        return printTimeOffset(ts, gPrevTs);
      };
    } else {
      LOG(ERROR) << "Invalid time format. Timestamps will not be shown.";
    }
  }

  folly::EventBase evb;
  ParserMap parserMap(msgReady);
  FifoReaderManager fifoManager(evb, parserMap,
                                gSettings.fifoRoot, std::move(filenamePattern));

  evb.loopForever();
}

} // anonymous namespace

int main(int argc, char **argv) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sa.sa_handler = cleanExit;
  for (auto sig : {SIGINT, SIGABRT, SIGQUIT, SIGPIPE}) {
    sigaction(sig, &sa, nullptr);
  }

  google::InitGoogleLogging(argv[0]);
  gSettings = parseOptions(argc, argv);

  run();

  return 0;
}
