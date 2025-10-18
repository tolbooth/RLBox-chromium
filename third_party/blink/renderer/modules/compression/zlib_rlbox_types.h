// Copyright 2025 tolbooth
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_

// RLBox configuration - must be defined before rlbox includes
#define RLBOX_SINGLE_THREADED_INVOCATIONS
#define RLBOX_USE_STATIC_CALLS() rlbox_noop_sandbox_lookup_symbol

// Include RLBox headers
#include "third_party/rlbox/src/code/include/rlbox.hpp"
#include "third_party/rlbox/src/code/include/rlbox_noop_sandbox.hpp"

// Define base types for zlib library using the noop sandbox
// This must be defined exactly once for the entire program
RLBOX_DEFINE_BASE_TYPES_FOR(zlib, noop)

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_ZLIB_RLBOX_TYPES_H_
