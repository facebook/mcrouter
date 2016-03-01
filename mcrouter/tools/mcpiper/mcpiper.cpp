/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <signal.h>

#include <cstring>
#include <iostream>

#include <boost/program_options.hpp>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/io/async/EventBase.h>
#include <folly/IPAddress.h>
#include <folly/SocketAddress.h>

#include "mcrouter/lib/mc/msg.h"
#include "mcrouter/tools/mcpiper/AnsiColorCodeStream.h"
#include "mcrouter/tools/mcpiper/Color.h"
#include "mcrouter/tools/mcpiper/Config.h"
#include "mcrouter/tools/mcpiper/FifoReader.h"
#include "mcrouter/tools/mcpiper/MessagePrinter.h"
#include "mcrouter/tools/mcpiper/PrettyFormat.h"
#include "mcrouter/tools/mcpiper/StyledString.h"
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

// Globals
std::pair<uint64_t, uint64_t> gStats{0, 0};

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
    std::cout << "exit" << std::endl
              << gStats.first << " messages received, "
              << gStats.second << " printed" << std::endl;
  }

  exit(status);
}

MessagePrinter::Options getOptions(const Settings& settings) {
  MessagePrinter::Options options;

  options.numAfterMatch = settings.numAfterMatch;
  options.quiet = settings.quiet;
  options.maxMessages = settings.maxMessages;
  options.disableColor = settings.disableColor;

  // Time Function
  static struct timeval prevTs = {0, 0};
  if (!settings.timeFormat.empty()) {
    if (settings.timeFormat == "absolute") {
      options.printTimeFn = printTimeAbsolute;
    } else if (settings.timeFormat == "diff") {
      options.printTimeFn = [](const struct timeval& ts) {
        return printTimeDiff(ts, prevTs);
      };
      gettimeofday(&prevTs, nullptr);
    } else if (settings.timeFormat == "offset") {
      options.printTimeFn = [](const struct timeval& ts) {
        return printTimeOffset(ts, prevTs);
      };
    } else {
      LOG(ERROR) << "Invalid time format. absolute|diff|offset expected, got "
                 << settings.timeFormat << ". Timestamps will not be shown.";
    }
  }

  // Exit function
  options.stopRunningFn = [](const MessagePrinter& printer) {
    gStats = printer.getStats();
    cleanExit(0);
  };

  return options;
}

MessagePrinter::Filter getFilter(const Settings& settings) {
  MessagePrinter::Filter filter;

  filter.valueMinSize = settings.valueMinSize;
  filter.invertMatch = settings.invertMatch;

  // Host
  if (!settings.host.empty()) {
    try {
      filter.host = folly::IPAddress(settings.host);
      std::cout << "Host: " << filter.host.toFullyQualified() << std::endl;
    } catch (...) {
      LOG(ERROR) << "Invalid IP address provided: " << filter.host;
      exit(1);
    }
  }

  // Port
  if (settings.port != 0) {
    filter.port = settings.port;
    std::cout << "Port: " << filter.port << std::endl;
  }

  // Builds data pattern
  filter.pattern = buildRegex(settings.matchExpression, settings.ignoreCase);
  if (filter.pattern) {
    if (settings.invertMatch) {
      std::cout << "Don't match: ";
    } else {
      std::cout << "Match: ";
    }
    std::cout << *filter.pattern << std::endl;
  }

  return filter;
}

void run(Settings settings) {
  // Builds filename pattern
  auto filenamePattern = buildRegex(settings.filenamePattern,
                                    settings.ignoreCase);
  if (filenamePattern) {
    std::cout << "Filename pattern: " << *filenamePattern << std::endl;
  }

  MessagePrinter messagePrinter(getOptions(settings),
                                getFilter(settings),
                                createValueFormatter());
  std::unordered_map<uint64_t, SnifferParser<MessagePrinter>> parserMap;

  // Callback from fifoManager. Read the data and feed the correct parser.
  auto fifoReaderCallback =
    [&parserMap, &messagePrinter](uint64_t connectionId,
                                  uint64_t packetId,
                                  folly::SocketAddress from,
                                  folly::SocketAddress to,
                                  folly::ByteRange data) {
      auto it = parserMap.find(connectionId);
      if (it == parserMap.end()) {
        it = parserMap.emplace(std::piecewise_construct,
                               std::forward_as_tuple(connectionId),
                               std::forward_as_tuple(messagePrinter)).first;
      }
      auto& snifferParser = it->second;

      if (packetId == 0) {
        snifferParser.parser().reset();
      }

      snifferParser.setAddresses(std::move(from), std::move(to));
      snifferParser.parser().parse(data);
    };

  folly::EventBase eventBase;
  FifoReaderManager fifoManager(eventBase, fifoReaderCallback,
                                settings.fifoRoot, std::move(filenamePattern));

  while (eventBase.loopOnce()) {
    // Update stats
    gStats = messagePrinter.getStats();
  }
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

  run(parseOptions(argc, argv));

  return 0;
}
