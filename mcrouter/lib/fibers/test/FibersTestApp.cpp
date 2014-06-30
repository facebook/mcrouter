/**
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */
#include <iostream>
#include <queue>

#include "folly/Memory.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/fibers/SimpleLoopController.h"

using namespace facebook::memcache;

struct Application {
 public:
  Application ()
      : fiberManager(folly::make_unique<SimpleLoopController>()),
        toSend(20),
        maxOutstanding(5) {
  }

  void loop() {
    if (pendingRequests.size() == maxOutstanding || toSend == 0) {
      if (pendingRequests.empty()) {
        return;
      }
      intptr_t value = rand()%1000;
      std::cout << "Completing request with data = " << value << std::endl;

      pendingRequests.front().setValue(value);
      pendingRequests.pop();
    } else {
      static size_t id_counter = 1;
      size_t id = id_counter++;
      std::cout << "Adding new request with id = " << id << std::endl;

      fiberManager.addTask([this, id]() {
          std::cout << "Executing fiber with id = " << id << std::endl;

          auto result1 = fiberManager.await(
            [this](FiberPromise<int> fiber) {
              pendingRequests.push(std::move(fiber));
            });

          std::cout << "Fiber id = " << id
                    << " got result1 = " << result1 << std::endl;

          auto result2 = fiberManager.await
            ([this](FiberPromise<int> fiber) {
              pendingRequests.push(std::move(fiber));
            });
          std::cout << "Fiber id = " << id
                    << " got result2 = " << result2 << std::endl;
         });

      if (--toSend == 0) {
        auto& loopController =
          dynamic_cast<SimpleLoopController&>(fiberManager.loopController());
        loopController.stop();
      }
    }
  }

  FiberManager fiberManager;

  std::queue<FiberPromise<int>> pendingRequests;
  size_t toSend;
  size_t maxOutstanding;
};

int main() {
  Application app;

  auto loop = [&app]() {
    app.loop();
  };

  auto& loopController =
    dynamic_cast<SimpleLoopController&>(app.fiberManager.loopController());

  loopController.loop(std::move(loop));

  return 0;
}
