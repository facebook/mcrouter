/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "mcrouter/Server.h"
#include "mcrouter/options.h"
#include "mcrouter/standalone_options.h"

namespace facebook {
namespace memcache {
namespace mcrouter {

// Exit status constants
constexpr int kExitStatusTransientError = 2;
constexpr int kExitStatusUnrecoverableError = 3;

/**
 * Whether or not we are running on validate-config mode. Validate-config mode
 * is a mode where this binary will run just to validate configs.
 * It's useful just for testing config changes.
 */
enum class ValidateConfigMode {
  /**
   * We are *not* running on validate config mode. Just run mcrouter normally.
   */
  None,

  /**
   * In this mode, we will *not* run mcrouter. We will just validate configs
   * and exit the program (even if the config is valid).
   */
  Exit,

  /**
   * In this mode, we will run config validation and, it the configs are 100%
   * valid, we will run mcrouter.
   * The program will terminate if there's anything wrong with the configs (
   * e.g. we will NOT try to run configs from disk if configs are invalid).
   */
  Run
};

/**
 * This struct contains the options parsed out of command line arguments.
 */
struct CmdLineOptions {
  // the flavor to use (or empty string if none was provided).
  std::string flavor;

  // the libmcrouter options overrides (they will override flavor options).
  std::unordered_map<std::string, std::string> libmcrouterOptionsOverrides;

  // the stnadalone mcrouter options overrides (they will override flavor
  // options).
  std::unordered_map<std::string, std::string> standaloneOptionsOverrides;

  // the list of invalid/unrecognized option overrides.
  std::unordered_set<std::string> unrecognizedOptions;

  // in which mode we should run.
  ValidateConfigMode validateConfigMode{ValidateConfigMode::None};

  // the name of the binary (i.e. argv[0]).
  std::string programName;

  // the name of the package (if none is provided, will use argv[0]).
  std::string packageName;

  // the raw command line arguments (it doesn't include the name of the binary).
  std::string commandArgs;

  // the service name we want to override
  std::string serviceName;

  // additional config params to merge into config_params after resolution.
  // Same format as --config-params: "name1:value1,name2:value2"
  std::string additionalConfigParams;
};

/**
 * Parses the command line arguments.
 *
 * @param argc      argc argument to your main.
 * @param argv      argv argument to your main.
 * @param pkgName   The name of the package. If none is provided, argv[0] will
 *                  be used.
 */
CmdLineOptions
parseCmdLineOptions(int argc, char** argv, std::string pkgName = "");

/**
 * Fills in libmcrouterOptions and standaloneOptions by first reading data from
 * the flavor (in cmdLineOpts.flavor), and then applying the command-line
 * overrides (in cmdLineOpts.libmcrouterOptionsOverrides and
 * cmdLineOpts.standaloneOptionsOverrides).
 *
 * @param cmdLineOpts             The result of parseCmdLineOptions() funciton.
 * @param libmcrouterOptionsDict  Output argument with the final libmcrouter
 *                                options.
 * @param standaloneOptionsDict   Output argument with the final standalone
 *                                mcrouter options.
 */
void getFlavorOptionsAndApplyOverrides(
    const CmdLineOptions& cmdLineOpts,
    std::unordered_map<std::string, std::string>& libmcrouterOptionsDict,
    std::unordered_map<std::string, std::string>& standaloneOptionsDict);

/**
 * Process-wide setup that must precede buildStandaloneOptions(), because
 * building options may already report failures: redirects stderr to log_file
 * (if set) and installs the failure service context and handler.
 * Call once per process.
 *
 * @param cmdLineOpts             The result of parseCmdLineOptions() function.
 * @param standaloneOptionsDict   The standalone mcrouter options dict whose
 *                                log_file applies to the whole process.
 */
void initStandaloneProcessEarly(
    const CmdLineOptions& cmdLineOpts,
    const std::unordered_map<std::string, std::string>& standaloneOptionsDict);

/**
 * Builds one router's options objects from its option dicts and reports any
 * option errors. Exits the process if the options are invalid.
 * May be called once per router in the same process. Must follow
 * initStandaloneProcessEarly().
 *
 * Parameters are as for setupStandaloneMcrouter().
 */
void buildStandaloneOptions(
    const std::string& serviceName,
    const CmdLineOptions& cmdLineOpts,
    const std::unordered_map<std::string, std::string>& libmcrouterOptionsDict,
    const std::unordered_map<std::string, std::string>& standaloneOptionsDict,
    McrouterOptions& libmcrouterOptions,
    McrouterStandaloneOptions& standaloneOptions);

/**
 * Process-wide setup that follows buildStandaloneOptions(): initializes ssl,
 * seeds rand(), adds the validate-config failure handler and, unless
 * validating and exiting, runs standaloneInit() and records the command line
 * for stats. Call once per process, after every router's options are built,
 * with the options that should own process-wide settings such as rss_limit_mb.
 *
 * @param cmdLineOpts         The result of parseCmdLineOptions() function.
 * @param libmcrouterOptions  A libmcrouter options object built by
 *                            buildStandaloneOptions().
 * @param standaloneOptions   A standalone mcrouter options object built by
 *                            buildStandaloneOptions().
 */
void initStandaloneProcess(
    const CmdLineOptions& cmdLineOpts,
    const McrouterOptions& libmcrouterOptions,
    const McrouterStandaloneOptions& standaloneOptions);

/**
 * Setup standalone mcrouter.
 * It doesn't run the standalone mcrouter server. It just perform the necessary
 * initializetion, such as: setup logging file, report any invalid command-line
 * argumnets, initialize ssl, etc.
 * Equivalent to initStandaloneProcessEarly(), buildStandaloneOptions() and
 * initStandaloneProcess(), in that order, for a single-router process.
 *
 * @param serviceName             Mcrouter's service_name.
 * @param cmdLineOpts             The result of parseCmdLineOptions() funciton.
 * @param libmcrouterOptionsDict  The final libmcrouter options, as returned by
 *                                getFlavorOptionsAndApplyOverrides().
 * @param standaloneOptionsDict   The final standalone mcrouter options, as
 *                                returned by
 *                                getFlavorOptionsAndApplyOverrides().
 * @param libmcrouterOptions      Output argument with the final libmcrouter
 *                                options object.
 * @param standaloneOptions       Output argument with the final standalone
 *                                mcrouter options object.
 */
void setupStandaloneMcrouter(
    const std::string& serviceName,
    const CmdLineOptions& cmdLineOpts,
    const std::unordered_map<std::string, std::string>& libmcrouterOptionsDict,
    const std::unordered_map<std::string, std::string>& standaloneOptionsDict,
    McrouterOptions& libmcrouterOptions,
    McrouterStandaloneOptions& standaloneOptions);

/**
 * Starts standalone mcrouter.
 * Note: this function will just return when standalone mcrouter is shutdown.
 *
 * @param cmdLineOpts         The result of parseCmdLineOptions() funciton.
 * @param libmcrouterOptions  The final libmcrouter options object.
 * @param standaloneOptions   The final standalone mcrouter options object.
 * @param preRunCb            Callback called before starting the server.
 */
void runStandaloneMcrouter(
    const CmdLineOptions& cmdLineOpts,
    const McrouterOptions& libmcrouterOptions,
    const McrouterStandaloneOptions& standaloneOptions,
    StandalonePreRunCb preRunCb = nullptr);

} // namespace mcrouter
} // namespace memcache
} // namespace facebook
