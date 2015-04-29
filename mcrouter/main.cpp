/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <event.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/Memory.h>
#include <folly/Range.h>
#include <folly/ScopeGuard.h>

#include "mcrouter/async.h"
#include "mcrouter/config.h"
#include "mcrouter/flavor.h"
#include "mcrouter/lib/fbi/error.h"
#include "mcrouter/lib/fbi/fb_cpu_util.h"
#include "mcrouter/ManagedModeUtil.h"
#include "mcrouter/McrouterInstance.h"
#include "mcrouter/McrouterLogFailure.h"
#include "mcrouter/options.h"
#include "mcrouter/proxy.h"
#include "mcrouter/server.h"
#include "mcrouter/standalone_options.h"
#include "mcrouter/stats.h"

using namespace facebook::memcache;
using namespace facebook::memcache::mcrouter;

using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

#define EXIT_STATUS_TRANSIENT_ERROR 2
#define EXIT_STATUS_UNRECOVERABLE_ERROR 3

static McrouterOptions opts;
static McrouterStandaloneOptions standaloneOpts;

#define print_usage(opt, desc) fprintf(stderr, "\t%*s%s\n", -49, opt, desc)

static void print_usage_and_die(char* progname, int errorCode) {

  fprintf(stderr, "%s\n"
          "usage: %s [options] -p port(s) -f config\n\n",
          MCROUTER_PACKAGE_STRING, progname);

  fprintf(stderr, "libmcrouter options:\n");

  auto opt_data = McrouterOptions::getOptionData();
  auto standalone_opt_data = McrouterStandaloneOptions::getOptionData();
  opt_data.insert(opt_data.end(), standalone_opt_data.begin(),
                  standalone_opt_data.end());
  std::string current_group;
  for (auto& opt : opt_data) {
    if (!opt.long_option.empty()) {
      if (current_group != opt.group) {
        current_group = opt.group;
        fprintf(stderr, "\n  %s\n", current_group.c_str());
      }
      if (opt.short_option) fprintf(stderr, "\t-%c,", opt.short_option);
      else fprintf(stderr, "\t   ");

      fprintf(stderr, " --%*s %s", -42, opt.long_option.c_str(),
              opt.docstring.c_str());

      if (opt.type != McrouterOptionData::Type::toggle)
        fprintf(stderr, " [default: %s]", opt.default_value.c_str());

      fprintf(stderr, "\n");
    }
  }

  fprintf(stderr, "\nMisc options:\n");


  print_usage("    --proxy-threads", "Like --num-proxies, but also accepts"
              " 'auto' to start one thread per core");
  print_usage("-D, --debug-level", "set debug level");
  print_usage("-d, --debug", "increase debug level (may repeat)");
  print_usage("-h, --help", "help");
  print_usage("-V, --version", "version");
  print_usage("-v, --verbosity", "Set verbosity of VLOG");
  print_usage("    --validate-config", "Check config and exit immediately"
              " with good or error status");

  fprintf(stderr, "\nRETURN VALUE\n");
  print_usage("2", "On a problem that might be resolved by restarting later.");
  static_assert(2 == EXIT_STATUS_TRANSIENT_ERROR,
                "Transient error status must be 2");
  print_usage("3", "On a problem that will definitely not be resolved by "
              "restarting.");
  static_assert(3 == EXIT_STATUS_UNRECOVERABLE_ERROR,
                "Unrecoverable error status must be 3");
  exit(errorCode);
}

// output the unimplemented option
void unimplemented(char ch, struct option long_options[], int long_index) {
  // long_index stores whether a long option was used
  if (long_index == -1) {
    fprintf(stderr, "Option %c is unimplemented\n", ch);
  }
  else {
    fprintf(stderr, "Option %s is unimplemented\n", long_options[long_index].name);
  }
}

char* construct_arg_string(int argc, char **argv) {
  char* buf = nullptr;
  char* c = nullptr;
  int i = 0;
  int len = 0;

  for (i = 1; i < argc; i++) {
    // Adding +1 after each arg leaves room for spaces and a \0 at the end
    len += strlen(argv[i]) + 1;
  }

  buf = (char*)malloc(sizeof(char) * len);
  if (buf == nullptr) {
    return nullptr;
  }

  c = buf;
  for (i = 1; i < argc; i++) {
    strcpy(c, argv[i]);
    c += strlen(argv[i]);

    if (i < argc - 1) {
      *(c++) = ' ';
    } else {
      *(c++) = '\0';
    }
  }

  return buf;
}

static void parse_options(int argc,
                          char** argv,
                          unordered_map<string, string>& option_dict,
                          unordered_map<string, string>& standalone_option_dict,
                          unordered_set<string>& unrecognized_options,
                          int* validate_configs,
                          string* flavor) {

  vector<option> long_options = {
    { "debug",                       0, nullptr, 'd'},
    { "debug-level",                 1, nullptr, 'D'},
    { "verbosity",                   1, nullptr, 'v'},
    { "help",                        0, nullptr, 'h'},
    { "version",                     0, nullptr, 'V'},
    { "validate-config",             0, nullptr,  0 },
    { "proxy-threads",               1, nullptr,  0 },

    // Deprecated or not supported
    { "gets",                        0, nullptr,  0 },
    { "skip-sanity-checks",          0, nullptr,  0 },
    { "retry-timeout",               1, nullptr,  0 },
  };

  string optstring = "dD:v:hV";

  // Append auto-generated options to long_options and optstring
  auto option_data = McrouterOptions::getOptionData();
  auto standalone_data = McrouterStandaloneOptions::getOptionData();
  auto combined_option_data = option_data;
  combined_option_data.insert(combined_option_data.end(),
                              standalone_data.begin(),
                              standalone_data.end());
  for (auto& opt : combined_option_data) {
    if (!opt.long_option.empty()) {
      int extra_arg = (opt.type == McrouterOptionData::Type::toggle ? 0 : 1);
      long_options.push_back(
        {opt.long_option.c_str(), extra_arg, nullptr, opt.short_option});

      if (opt.short_option) {
        optstring.push_back(opt.short_option);
        if (extra_arg) optstring.push_back(':');
      }
    }
  }

  long_options.push_back({0, 0, 0, 0});

  int debug_level = FBI_LOG_NOTIFY;

  int long_index = -1;
  int c;
  while ((c = getopt_long(argc, argv, optstring.c_str(), long_options.data(),
                          &long_index)) != -1) {
    switch (c) {
    case 'd':
      debug_level = std::max(debug_level + 10, FBI_LOG_DEBUG);
      break;
    case 'D':
      debug_level = strtol(optarg, nullptr, 10);
    case 'v':
      FLAGS_v = folly::to<int>(optarg);
      break;

    case 'h':
      print_usage_and_die(argv[0], /* errorCode */ 0);
      break;
    case 'V':
      printf("%s\n", MCROUTER_PACKAGE_STRING);
      exit(0);
      break;

    case 0:
    default:
      if (long_index != -1 &&
          strcmp("constantly-reload-configs",
                 long_options[long_index].name) == 0) {
        LOG(ERROR) << "CRITICAL: You have enabled constantly-reload-configs. "
          "This undocumented feature is incredibly dangerous. "
          "It will result in massively increased CPU consumption. "
          "It will trigger lots of edge cases, surely causing hard failures. "
          "If you're using this for *anything* other than testing, "
          "please resign.";
      }

      /* If the current short/long option is found in opt_data,
         set it in the opt_dict and return true.  False otherwise. */
      auto find_and_set = [&] (const vector<McrouterOptionData>& opt_data,
                               unordered_map<string, string>& opt_dict) {
        for (auto& opt : opt_data) {
          if (!opt.long_option.empty()) {
            if ((opt.short_option && opt.short_option == c) ||
                (!opt.long_option.empty() && long_index != -1 &&
                 opt.long_option == long_options[long_index].name)) {

              if (opt.type == McrouterOptionData::Type::toggle) {
                opt_dict[opt.name] = (opt.default_value == "false" ?
                                      "1" : "0");
              } else {
                opt_dict[opt.name] = optarg;
              }

              return true;
            }
          }
        }
        return false;
      };

      if (find_and_set(option_data, option_dict)) {
        break;
      }

      if (find_and_set(standalone_data, standalone_option_dict)) {
        break;
      }

      if (long_index == -1) {
        unrecognized_options.insert(argv[optind - 1]);
      } else if (strcmp("proxy-threads",
                        long_options[long_index].name) == 0) {
        if (strcmp(optarg, "auto") == 0) {
          int nprocs = sysconf(_SC_NPROCESSORS_ONLN);
          if (nprocs > 0) {
            option_dict["num_proxies"] = std::to_string(nprocs);
          } else {
            LOG(INFO) << "Couldn't determine how many cores are available. "
                         "Defaulting to " << DEFAULT_NUM_PROXIES <<
                         " proxy thread(s)";
          }
        } else {
          option_dict["num_proxies"] = optarg;
        }
      } else if (strcmp("validate-config",
                        long_options[long_index].name) == 0) {
        *validate_configs = 1;
      } else if (strcmp("retry-timeout",
                        long_options[long_index].name) == 0) {
        LOG(WARNING) << "--retry-timeout is deprecated, use"
                        " --probe-timeout-initial";
        option_dict["probe_delay_initial_ms"] = optarg;
      } else {
        unrecognized_options.insert(argv[optind - 1]);
      }
    }
    long_index = -1;
  }

  standalone_option_dict["debug_level"] = std::to_string(debug_level);

  /* getopt permutes argv so that all non-options are at the end.
     For now we only expect one non-option argument, so look at the last one. */
  *flavor = string();
  if (optind < argc && argv[optind]) {
    *flavor = string(argv[optind]);
  }
  if (optind + 1 < argc) {
    LOG(ERROR) << "Expected only one non-option argument";
  }
}

void daemonize() {
  if (getppid() == 1) {
    return;
  }
  switch (fork()) {
  case 0:
    PCHECK(setsid() != -1);
    /* stdin and stdout redirected to /dev/null; we use stderr for logging */
    close(0);
    PCHECK(open("/dev/null", O_RDWR) != -1);
    PCHECK(dup2(0, 1) != -1);
    break;
  case -1:
    fprintf(stderr, "Can't fork background process\n");
    exit(EXIT_STATUS_TRANSIENT_ERROR);
  default: /* Parent process */
    DLOG(INFO) << "Running in the background";
    exit(0);
  }
}

/** @return 0 on failure */
static int validate_options() {
  if (opts.num_proxies == 0) {
    LOG(ERROR) << "invalid number of proxy threads";
    return 0;
  }
  if (standaloneOpts.ports.empty() &&
      standaloneOpts.listen_sock_fd < 0) {
    LOG(ERROR) << "invalid ports";
    return 0;
  }

  if (opts.keepalive_idle_s <= 0 || opts.keepalive_interval_s <= 0 ||
      opts.keepalive_cnt < 0) {
    LOG(ERROR) << "invalid keepalive options";
    return 0;
  }
  return 1;
}

/** Set the fdlimit before any libevent setup because libevent uses the fd
    limit as a initial allocation buffer and if the limit is too low, then
    libevent realloc's the buffer which may cause epoll to copy out to the
    wrong address returning bogus data to user space, or it's just a bug in
    libevent. It's simpler to increase the limit than to fix libevent.
    libev does not suffer from this problem. */
static void raise_fdlimit() {
  struct rlimit rlim;
  if (getrlimit(RLIMIT_NOFILE, &rlim) == -1) {
    perror("Failed to getrlimit RLIMIT_NOFILE");
    exit(EXIT_STATUS_TRANSIENT_ERROR);
  }
  if (rlim.rlim_cur < standaloneOpts.fdlimit) {
    rlim.rlim_cur = standaloneOpts.fdlimit;
  }
  if (rlim.rlim_max < rlim.rlim_cur) {
    if (getuid() == 0) { // only root can raise the hard limit
      rlim.rlim_max = rlim.rlim_cur;
    } else {
      LOG(WARNING)  << "Warning: RLIMIT_NOFILE maxed out at " <<
                       rlim.rlim_max << " (you asked for " <<
                       standaloneOpts.fdlimit << ")";
      rlim.rlim_cur = rlim.rlim_max;
    }
  }
  if (setrlimit(RLIMIT_NOFILE, &rlim) == -1) {
    perror("Warning: failed to setrlimit RLIMIT_NOFILE");
    if (getuid() == 0) {
      // if it fails for root, something is seriously wrong
      exit(EXIT_STATUS_TRANSIENT_ERROR);
    }
  }
}

static void error_flush_cb(const fbi_err_t *err) {
  fbi_dbg_log("mcrouter", err->source, "", err->lineno,
              fbi_errtype_to_string(err->type), 0, 0, "%s",
              nstring_safe(&err->message));
}

void notify_command_line(int argc, char ** argv) {
  size_t len = 0;
  int ii;
  char *cc;

  for (ii = 0; ii < argc; ii++) {
    len += 1 + strlen(argv[ii]);
  }
  char *cmd_line = (char *) malloc(len + 1);
  char *ptr = cmd_line;
  for (ii = 0; ii < argc; ii++) {
    cc = argv[ii];
    while(*cc) {
      *ptr++ = *cc++;
    }
    *ptr++ = ' ';
  }
  *ptr = '\0';

  LOG(INFO) << cmd_line;
  free(cmd_line);
}

void on_assert_fail(const char *msg) {
  logFailure(failure::Category::kBrokenLogic, msg);
}

int main(int argc, char **argv) {
  FLAGS_v = 1;
  FLAGS_logtostderr = 1;
  google::InitGoogleLogging(argv[0]);

  unordered_map<string, string> option_dict, st_option_dict,
    cmdline_option_dict, cmdline_st_option_dict;
  unordered_set<string> unrecognized_options;
  int validate_configs = 0;
  std::string flavor;

  parse_options(argc, argv, cmdline_option_dict, cmdline_st_option_dict,
                unrecognized_options, &validate_configs, &flavor);

  if (flavor.empty()) {
    option_dict = cmdline_option_dict;
    st_option_dict = cmdline_st_option_dict;
  } else {
    read_standalone_flavor(flavor, option_dict, st_option_dict);
    for (auto& it : cmdline_option_dict) {
      option_dict[it.first] = it.second;
    }
    for (auto& it : cmdline_st_option_dict) {
      st_option_dict[it.first] = it.second;
    }
  }
  auto log_file = st_option_dict.find("log_file");
  if (log_file != st_option_dict.end()
      && !log_file->second.empty()) {
    auto fd = open(log_file->second.c_str(), O_CREAT | O_WRONLY | O_APPEND,
                   S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd == -1) {
      LOG(ERROR) << "Couldn't open log file " << log_file->second <<
                    " for writing: " << strerror(errno);
    } else {
      LOG(INFO) << "Logging to " << log_file->second;
      PCHECK(dup2(fd, STDERR_FILENO));
    }
  }

  auto commandArgs = construct_arg_string(argc, argv);
  failure::setServiceContext("mcrouter",
                             std::string(argv[0]) + " " + commandArgs);
  failure::setHandler(failure::handlers::logToStdError());

  auto check_errors =
  [&] (const vector<McrouterOptionError>& errors) {
    if (!errors.empty()) {
      for (auto& e : errors) {
        logFailure(failure::Category::kInvalidOption,
                   "Option parse error: {}={}, {}",
                   e.requestedName, e.requestedValue, e.errorMsg);
      }
    }
  };

  auto standaloneErrors = standaloneOpts.updateFromDict(st_option_dict);
  auto errors = opts.updateFromDict(option_dict);

  if (opts.enable_failure_logging) {
    initFailureLogger();
  }

  check_errors(standaloneErrors);
  check_errors(errors);

  for (const auto& option : unrecognized_options) {
    logFailure(failure::Category::kInvalidOption, "Unrecognized option: {}",
               option);
  }

  srand(time(nullptr) + getpid());

  fbi_set_err_flush_cb(error_flush_cb);

  // act on options
  if (standaloneOpts.log_file != "") {
    nstring_t logfile
      = NSTRING_INIT((char*)standaloneOpts.log_file.c_str());
    fbi_set_debug_logfile(&logfile);
  }

  fbi_set_debug(standaloneOpts.debug_level);
  fbi_set_debug_date_format(fbi_date_utc);
  fbi_set_assert_hook(&on_assert_fail);

  // do this immediately after setting up log file
  notify_command_line(argc, argv);

  if (!standaloneInit(opts) || !validate_options()) {
    print_usage_and_die(argv[0], EXIT_STATUS_UNRECOVERABLE_ERROR);
  }

  if (validate_configs) {
    failure::addHandler(failure::handlers::throwLogicError());
  }

  LOG(INFO) << MCROUTER_PACKAGE_STRING << " startup (" << getpid() << ")";

  if (standaloneOpts.background && !validate_configs) {
    daemonize();
  }

  // Managed mode
  if (standaloneOpts.managed && !validate_configs) {
    spawnManagedChild();
    LOG(INFO) << "forked (" << getpid() << ")";
  }

  raise_fdlimit();
  opts.standalone = 1;

  // Set the router port as part of the router id.
  std::string port_str = "0";
  if (!standaloneOpts.ports.empty()) {
    port_str = std::to_string(standaloneOpts.ports[0]);
  }

  opts.service_name = "mcrouter";
  if (flavor.empty()) {
    opts.router_name = port_str.c_str();
  }

  /*
   * We know that mc_msg_t's are guaranteed to
   * only ever be used by one thread, so disable
   * atomic refcounts
   */
  mc_msg_use_atomic_refcounts(0);

  auto router = McrouterInstance::init("standalone", opts);
  if (router == nullptr) {
    LOG(ERROR) << "CRITICAL: Failed to initialize mcrouter!";
    exit(EXIT_STATUS_TRANSIENT_ERROR);
  } else if (validate_configs) {
    /* Only validating config, exit with good status immediately */
    _exit(0);
  }

  set_standalone_args(commandArgs);
  router->addStartupOpts(standaloneOpts.toDict());

  runServer(standaloneOpts, *router);
}
