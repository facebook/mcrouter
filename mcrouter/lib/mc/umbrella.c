/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "umbrella.h"

uint32_t const umbrella_op_from_mc[UM_NOPS] = {
#define UM_OP(mc, um) [mc] = um,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_op_to_mc[UM_NOPS] = {
#define UM_OP(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_res_from_mc[mc_nres] = {
#define UM_RES(mc, um) [mc] = um,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

uint32_t const umbrella_res_to_mc[mc_nres] = {
#define UM_RES(mc, um) [um] = mc,
#include "mcrouter/lib/mc/umbrella_conv.h" /* nolint */
};

