/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "mcrouter/lib/config/RouteHandleBuilder.h"
#include "mcrouter/lib/routes/AllAsyncRoute.h"
#include "mcrouter/lib/routes/AllFastestRoute.h"
#include "mcrouter/lib/routes/AllInitialRoute.h"
#include "mcrouter/lib/routes/AllMajorityRoute.h"
#include "mcrouter/lib/routes/AllSyncRoute.h"
#include "mcrouter/lib/routes/ErrorRoute.h"
#include "mcrouter/lib/routes/FailoverRoute.h"
#include "mcrouter/lib/routes/HashRoute.h"
#include "mcrouter/lib/routes/LatestRoute.h"
#include "mcrouter/lib/routes/MissFailoverRoute.h"
#include "mcrouter/lib/routes/NullRoute.h"
#include "mcrouter/lib/routes/RandomRoute.h"
