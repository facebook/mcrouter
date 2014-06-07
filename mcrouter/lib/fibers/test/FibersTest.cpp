#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "folly/Benchmark.h"
#include "folly/Memory.h"
#include "mcrouter/lib/fibers/AddTasks.h"
#include "mcrouter/lib/fibers/FiberManager.h"
#include "mcrouter/lib/fibers/SimpleLoopController.h"
#include "mcrouter/lib/fibers/WhenN.h"

using namespace facebook::memcache;

using folly::wangle::Try;

TEST(FiberManager, addTasksNoncopyable) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<std::unique_ptr<int>()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                return folly::make_unique<int>(i*2 + 1);
              }
            );
          }

          auto iter = fiber::addTasks(funcs.begin(), funcs.end());

          size_t n = 0;
          while (iter.hasNext()) {
            auto result = iter.awaitNext();
            EXPECT_EQ(2 * iter.getTaskID() + 1, *result);
            EXPECT_GE(2 - n, pendingFibers.size());
            ++n;
          }
          EXPECT_EQ(3, n);
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksThrow) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<int()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                if (i % 2 == 0) {
                  throw std::runtime_error("Runtime");
                }
                return i*2 + 1;
              }
            );
          }

          auto iter = fiber::addTasks(funcs.begin(), funcs.end());

          size_t n = 0;
          while (iter.hasNext()) {
            try {
              int result = iter.awaitNext();
              EXPECT_EQ(1, iter.getTaskID() % 2);
              EXPECT_EQ(2 * iter.getTaskID() + 1, result);
            } catch (...) {
              EXPECT_EQ(0, iter.getTaskID() % 2);
            }
            EXPECT_GE(2 - n, pendingFibers.size());
            ++n;
          }
          EXPECT_EQ(3, n);
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksVoid) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<void()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
              }
            );
          }

          auto iter = fiber::addTasks(funcs.begin(), funcs.end());

          size_t n = 0;
          while (iter.hasNext()) {
            iter.awaitNext();
            EXPECT_GE(2 - n, pendingFibers.size());
            ++n;
          }
          EXPECT_EQ(3, n);
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, addTasksVoidThrow) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<void()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                if (i % 2 == 0) {
                  throw std::runtime_error("");
                }
              }
            );
          }

          auto iter = fiber::addTasks(funcs.begin(), funcs.end());

          size_t n = 0;
          while (iter.hasNext()) {
            try {
              iter.awaitNext();
              EXPECT_EQ(1, iter.getTaskID() % 2);
            } catch (...) {
              EXPECT_EQ(0, iter.getTaskID() % 2);
            }
            EXPECT_GE(2 - n, pendingFibers.size());
            ++n;
          }
          EXPECT_EQ(3, n);
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenN) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<int()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                return i*2 + 1;
              }
            );
          }

          auto results = fiber::whenN(funcs.begin(), funcs.end(), 2);
          EXPECT_EQ(2, results.size());
          EXPECT_EQ(1, pendingFibers.size());
          for (size_t i = 0; i < 2; ++i) {
            EXPECT_EQ(results[i].first*2 + 1, results[i].second);
          }
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenNThrow) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<int()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                throw std::runtime_error("Runtime");
                return i*2+1;
              }
            );
          }

          try {
            fiber::whenN(funcs.begin(), funcs.end(), 2);
          } catch (...) {
            EXPECT_EQ(1, pendingFibers.size());
          }
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenNVoid) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<void()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
              }
            );
          }

          auto results = fiber::whenN(funcs.begin(), funcs.end(), 2);
          EXPECT_EQ(2, results.size());
          EXPECT_EQ(1, pendingFibers.size());
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenNVoidThrow) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<void()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                throw std::runtime_error("Runtime");
              }
            );
          }

          try {
            fiber::whenN(funcs.begin(), funcs.end(), 2);
          } catch (...) {
            EXPECT_EQ(1, pendingFibers.size());
          }
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenAll) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<int()>> funcs;
          for (size_t i = 0; i < 3; ++i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                return i*2+1;
              }
            );
          }

          auto results = fiber::whenAll(funcs.begin(), funcs.end());
          EXPECT_TRUE(pendingFibers.empty());
          for (size_t i = 0; i < 3; ++i) {
            EXPECT_EQ(i*2+1, results[i]);
          }
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenAllVoid) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<void()>> funcs;
          for (size_t i = 0; i < 3; ++ i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
              }
            );
          }

          fiber::whenAll(funcs.begin(), funcs.end());
          EXPECT_TRUE(pendingFibers.empty());
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, whenAny) {
  std::vector<FiberPromise<int>> pendingFibers;
  bool taskAdded = false;

  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  auto loopFunc = [&]() {
    if (!taskAdded) {
      manager.addTask(
        [&]() {
          std::vector<std::function<int()> > funcs;
          for (size_t i = 0; i < 3; ++ i) {
            funcs.push_back(
              [i, &pendingFibers]() {
                fiber::await([&pendingFibers](FiberPromise<int> promise) {
                    pendingFibers.push_back(std::move(promise));
                  });
                if (i == 1) {
                  throw std::runtime_error("This exception will be ignored");
                }
                return i*2+1;
              }
            );
          }

          auto result = fiber::whenAny(funcs.begin(), funcs.end());
          EXPECT_EQ(2, pendingFibers.size());
          EXPECT_EQ(2, result.first);
          EXPECT_EQ(2*2+1, result.second);
        }
      );
      taskAdded = true;
    } else if (pendingFibers.size()) {
      pendingFibers.back().setValue(0);
      pendingFibers.pop_back();
    } else {
      loopController.stop();
    }
  };

  loopController.loop(std::move(loopFunc));
}

TEST(FiberManager, runInMainContext) {
  /* Small stack */
  FiberManager::Options opts;
  opts.stackSize = 1024;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  bool bigStackFuncRun = false;

  auto bigStackFunc = [&]() {
    int bigArray[1024 * 1024];

    for (size_t i = 0; i < 1024 * 1024; ++i) {
      bigArray[i] = 0x1234;
    }

    for (size_t i = 0; i < 1024 * 1024; ++i) {
      EXPECT_EQ(bigArray[i], 0x1234);
    }

    EXPECT_FALSE(bigStackFuncRun);
    bigStackFuncRun = true;
  };

  manager.runInMainContext(bigStackFunc);
  EXPECT_TRUE(bigStackFuncRun);

  bigStackFuncRun = false;

  manager.addTask(
    [&bigStackFunc, &bigStackFuncRun]() {
      /* Would crash if run in fiber's context */
      fiber::runInMainContext(bigStackFunc);
      EXPECT_TRUE(bigStackFuncRun);
    }
  );

  loopController.loop(
    [&]() {
      loopController.stop();
    }
  );

  EXPECT_TRUE(bigStackFuncRun);
}

TEST(FiberManager, addTaskFinally) {
  /* Small stack */
  FiberManager::Options opts;
  opts.stackSize = 1024;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  bool bigStackFuncRun = false;

  auto bigStackFunc = [&]() {
    int bigArray[1024 * 1024];

    for (size_t i = 0; i < 1024 * 1024; ++i) {
      bigArray[i] = 0x1234;
    }

    for (size_t i = 0; i < 1024 * 1024; ++i) {
      EXPECT_EQ(bigArray[i], 0x1234);
    }

    EXPECT_FALSE(bigStackFuncRun);
    bigStackFuncRun = true;
  };

  manager.addTaskFinally(
    [&]() {
      return 1234;
    },
    [&](Try<int>&& result) {
      EXPECT_EQ(result.value(), 1234);

      /* Would crash if run in fiber's context */
      bigStackFunc();
      EXPECT_TRUE(bigStackFuncRun);
    }
  );

  loopController.loop(
    [&]() {
      loopController.stop();
    }
  );

  EXPECT_TRUE(bigStackFuncRun);
}

TEST(FiberManager, fibersPoolWithinLimit) {
  FiberManager::Options opts;
  opts.maxFibersPoolSize = 5;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  size_t fibersRun = 0;

  for (size_t i = 0; i < 5; ++i) {
    manager.addTask(
      [&]() {
        ++fibersRun;
      }
    );
  }
  loopController.loop(
    [&]() {
      loopController.stop();
    }
  );

  EXPECT_EQ(5, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());

  for (size_t i = 0; i < 5; ++i) {
    manager.addTask(
      [&]() {
        ++fibersRun;
      }
    );
  }
  loopController.loop(
    [&]() {
      loopController.stop();
    }
  );

  EXPECT_EQ(10, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());
}

TEST(FiberManager, fibersPoolOverLimit) {
  FiberManager::Options opts;
  opts.maxFibersPoolSize = 5;

  FiberManager manager(folly::make_unique<SimpleLoopController>(), opts);
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  size_t fibersRun = 0;

  for (size_t i = 0; i < 10; ++i) {
    manager.addTask(
      [&]() {
        ++fibersRun;
      }
    );
  }

  EXPECT_EQ(0, fibersRun);
  EXPECT_EQ(10, manager.fibersAllocated());
  EXPECT_EQ(0, manager.fibersPoolSize());

  loopController.loop(
    [&]() {
      loopController.stop();
    }
  );

  EXPECT_EQ(10, fibersRun);
  EXPECT_EQ(5, manager.fibersAllocated());
  EXPECT_EQ(5, manager.fibersPoolSize());
}

TEST(FiberManager, remoteFiberBasic) {
  FiberManager manager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(manager.loopController());

  int result[2];
  result[0] = result[1] = 0;
  folly::Optional<FiberPromise<int>> savedPromise[2];
  manager.addTask(
    [&] () {
      result[0] = fiber::await([&] (FiberPromise<int> promise) {
          savedPromise[0] = std::move(promise);
        });
    });
  manager.addTask(
    [&] () {
      result[1] = fiber::await([&] (FiberPromise<int> promise) {
          savedPromise[1] = std::move(promise);
        });
    });

  manager.loopUntilNoReady();

  EXPECT_TRUE(savedPromise[0].hasValue());
  EXPECT_TRUE(savedPromise[1].hasValue());
  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);

  std::thread remoteThread0{
    [&] () {
      savedPromise[0]->setValue(42);
    }
  };
  std::thread remoteThread1{
    [&] () {
      savedPromise[1]->setValue(43);
    }
  };
  remoteThread0.join();
  remoteThread1.join();
  EXPECT_EQ(0, result[0]);
  EXPECT_EQ(0, result[1]);
  /* Should only have scheduled once */
  EXPECT_EQ(1, loopController.remoteScheduleCalled());

  manager.loopUntilNoReady();
  EXPECT_EQ(42, result[0]);
  EXPECT_EQ(43, result[1]);
}

static size_t sNumAwaits;

void runBenchmark(size_t numAwaits, size_t toSend) {
  sNumAwaits = numAwaits;

  FiberManager fiberManager(folly::make_unique<SimpleLoopController>());
  auto& loopController =
    dynamic_cast<SimpleLoopController&>(fiberManager.loopController());

  std::queue<FiberPromise<int>> pendingRequests;
  static const size_t maxOutstanding = 5;

  auto loop = [&fiberManager, &loopController, &pendingRequests, &toSend]() {
    if (pendingRequests.size() == maxOutstanding || toSend == 0) {
      if (pendingRequests.empty()) {
        return;
      }
      pendingRequests.front().setValue(0);
      pendingRequests.pop();
    } else {
      fiberManager.addTask([&pendingRequests]() {
          for (size_t i = 0; i < sNumAwaits; ++i) {
            auto result = fiber::await(
              [&pendingRequests](FiberPromise<int> promise) {
                pendingRequests.push(std::move(promise));
              });
            assert(result == 0);
          }
        });

      if (--toSend == 0) {
        loopController.stop();
      }
    }
  };

  loopController.loop(std::move(loop));
}

BENCHMARK(FiberManagerBasicOneAwait, iters) {
  runBenchmark(1, iters);
}

BENCHMARK(FiberManagerBasicFiveAwaits, iters) {
  runBenchmark(5, iters);
}
