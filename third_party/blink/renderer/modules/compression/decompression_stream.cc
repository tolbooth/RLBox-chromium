// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/compression/decompression_stream.h"

#include "base/metrics/histogram_macros.h"
#include "third_party/blink/renderer/modules/compression/compression_format.h"
#include "third_party/blink/renderer/modules/compression/inflate_transformer.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "third_party/rlbox/src/code/include/rlbox.hpp"
#include "third_party/rlbox/src/code/include/rlbox_noop_sandbox.hpp"


// We're going to use RLBox in a single-threaded environment.
#define RLBOX_SINGLE_THREADED_INVOCATIONS
// The fixed configuration line we need to use for the noop sandbox.
// It specifies that all calls into the sandbox are resolved statically.
#define RLBOX_USE_STATIC_CALLS() rlbox_noop_sandbox_lookup_symbol

// Define base type for mylib using the noop sandbox
RLBOX_DEFINE_BASE_TYPES_FOR(zlib, noop)

namespace blink {

DecompressionStream* DecompressionStream::Create(
    ScriptState* script_state,
    const AtomicString& format,
    ExceptionState& exception_state) {
  return MakeGarbageCollected<DecompressionStream>(script_state, format,
                                                   exception_state);
}

ReadableStream* DecompressionStream::readable() const {
  return transform_->Readable();
}

WritableStream* DecompressionStream::writable() const {
  return transform_->Writable();
}

void DecompressionStream::Trace(Visitor* visitor) const {
  visitor->Trace(transform_);
  ScriptWrappable::Trace(visitor);
}

DecompressionStream::DecompressionStream(ScriptState* script_state,
                                         const AtomicString& format,
                                         ExceptionState& exception_state) {
  CompressionFormat inflate_format =
      LookupCompressionFormat(format, exception_state);
  if (exception_state.HadException())
    return;

  UMA_HISTOGRAM_ENUMERATION("Blink.Compression.DecompressionStream.Format",
                            inflate_format);

  transform_ = TransformStream::Create(
      script_state,
      MakeGarbageCollected<InflateTransformer>(script_state, inflate_format),
      exception_state);
}

}  // namespace blink
