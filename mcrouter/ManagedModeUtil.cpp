/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "ManagedModeUtil.h"

#include <signal.h>
#include <sys/eventfd.h>
#include <sys/wait.h>

#include <thread>

#include <glog/logging.h>

#include "mcrouter/lib/fbi/cpp/LogFailure.h"

namespace facebook { namespace memcache { namespace mcrouter {

namespace {

// Constants
const int SpawnWait = 10; // seconds between failed spawns in managed mode
const unsigned int TermSignalTimeout{3000000}; // microseconds

// Globals
std::atomic<pid_t> childPid{-1}; // PID of the current child process.
std::atomic<bool> running{true};

bool waitpidTimeout(int pid, unsigned int timeout) {
  // Exponential sleep.
  int time = 500;
  int remainingTimeout = timeout;

  int rv;
  while (remainingTimeout > 0) {
    rv = waitpid(pid, nullptr, WNOHANG);
    if (rv > 0 || (rv == -1 && errno == ECHILD)) {
      return true;
    }
    time = std::min(time, remainingTimeout);
    usleep(time); // usleep is guaranteed to break on signal.

    remainingTimeout -= time;
    time *= 2;
  }

  rv = waitpid(pid, nullptr, WNOHANG);
  return (rv > 0 || (rv == -1 && errno == ECHILD));
}

void termSignalHandler(int signal) {
  if (childPid > 0) {
    running = false;

    // Asures the above sets are done before going on.
    std::atomic_signal_fence(std::memory_order_seq_cst);

    kill(childPid, signal);
  }
}

void installSignalHandlers() {
  struct sigaction act;
  memset(&act, 0, sizeof(struct sigaction));

  // Shutdown signals
  act.sa_handler = termSignalHandler;
  for (auto sig : {SIGTERM, SIGINT, SIGQUIT}) {
    CHECK(!sigaction(sig, &act, nullptr));
  }

  // Child signal
  signal(SIGCHLD, SIG_IGN);
}

void uninstallSignalHandlers() {
  for (auto sig : {SIGTERM, SIGINT, SIGQUIT, SIGCHLD}) {
    signal(sig, SIG_DFL);
  }
}

} // anonymous namsepace

void spawnManagedChild() {
  installSignalHandlers();

  // Loops forever to make sure the parent process never leaves.
  while (true) {
    switch (childPid = fork()) {
    case -1:
      // error
      LOG_FAILURE("mcrouter", failure::Category::kSystemError,
                  "Can't spawn child process, sleeping");
      std::this_thread::sleep_for(std::chrono::seconds(SpawnWait));
      break;

    case 0:
      // child process. cleanup and continue with the startup logic.
      uninstallSignalHandlers();
      return;

    default:
      // parent process.
      LOG(INFO) << "Spawned child process " << childPid;

      int rv;
      do {
        rv = wait(nullptr);
      } while (rv == -1 && errno != ECHILD && running);

      if (running) {
        // Child died accidentally. Cleanup and restart.
        LOG(INFO) << "Child process " << childPid << " exited";
        waitpid(childPid, nullptr, 0);
      } else {
        // Child was killed. Shutdown parent.
        if (!waitpidTimeout(childPid, TermSignalTimeout)) {
          LOG_FAILURE("mcrouter", failure::Category::kSystemError,
              "Child process did not exit in {} microseconds. Sending SIGKILL.",
              TermSignalTimeout);
          kill(childPid, SIGKILL);
        }
        exit(0);
      }
      break;
    }
  }
}

bool shutdownFromChild() {
  // Makes sure we are in the child process.
  if (childPid == 0) {
    pid_t parentPid = getppid();
    if (kill(parentPid, SIGTERM) == 0) {
      return true;
    }
  }
  return false;
}

}}} // namespace facebook::memcache::mcrouter
