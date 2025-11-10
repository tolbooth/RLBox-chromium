// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/compression/deflate_transformer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "base/compiler_specific.h"
#include "base/trace_event/typed_macros.h"
#include "third_party/blink/renderer/bindings/core/v8/script_promise.h"
#include "third_party/blink/renderer/bindings/core/v8/to_v8_traits.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_binding_for_core.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_typedefs.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_union_arraybuffer_arraybufferview.h"
#include "third_party/blink/renderer/core/streams/transform_stream_default_controller.h"
#include "third_party/blink/renderer/core/streams/transform_stream_transformer.h"
#include "third_party/blink/renderer/core/typed_arrays/array_buffer_view_helpers.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_array_piece.h"
#include "third_party/blink/renderer/modules/compression/compression_format.h"
#include "third_party/blink/renderer/modules/compression/zlib_rlbox_types.h"
#include "third_party/blink/renderer/modules/compression/zlib_structs_for_rlbox.h"
#include "third_party/blink/renderer/platform/bindings/exception_state.h"
#include "v8/include/v8.h"

// Load struct field definitions for sandboxed z_stream access
rlbox_load_structs_from_library(zlib);

namespace blink {

DeflateTransformer::DeflateTransformer(ScriptState* script_state,
                                       CompressionFormat format,
                                       int level,
                                       rlbox_sandbox_zlib* sandbox)
    : script_state_(script_state),
      sandbox_(sandbox),
      sandboxed_stream_(nullptr),
      sandboxed_out_buffer_(nullptr),
      sandboxed_in_buffer_(nullptr) {
  DCHECK(level >= 1 && level <= 9);
  DCHECK(sandbox_);

  sandboxed_stream_ = sandbox_->malloc_in_sandbox<z_stream>();
  sandboxed_out_buffer_ = sandbox_->malloc_in_sandbox<uint8_t>(kBufferSize);

  // Set zalloc/zfree to Z_NULL to use default allocators within the sandbox
  z_stream temp_stream = {};
  temp_stream.zalloc = Z_NULL;
  temp_stream.zfree = Z_NULL;
  temp_stream.opaque = Z_NULL;

  // Copy initialized stream to sandbox
  rlbox::memcpy(*sandbox_, sandboxed_stream_, &temp_stream, sizeof(z_stream));

  // Prepare version string in sandbox for deflateInit2_
  const char* version_str = ZLIB_VERSION;
  size_t version_len = std::strlen(version_str) + 1;
  auto sandboxed_version = sandbox_->malloc_in_sandbox<char>(version_len);
  rlbox::strncpy(*sandbox_, sandboxed_version, version_str, version_len);

  // Compression format determines the number of window bits (base 2 of window size)
  constexpr int kWindowBits = 15;
  constexpr int kUseGzip = 16;
  int window_bits;
  switch (format) {
    case CompressionFormat::kDeflate:
      window_bits = kWindowBits;
      break;
    case CompressionFormat::kGzip:
      window_bits = kWindowBits + kUseGzip;
      break;
    case CompressionFormat::kDeflateRaw:
      window_bits = -kWindowBits;
      break;
  }

  // Call deflateInit2_ in sandbox
  auto result = sandbox_->invoke_sandbox_function(
      deflateInit2_, sandboxed_stream_, level, Z_DEFLATED, window_bits, 8,
      Z_DEFAULT_STRATEGY, sandboxed_version,
      static_cast<int>(sizeof(z_stream)));
  int err = result.unverified_safe_because(
      "Error code from deflateInit2. We handle all zlib error codes safely.");

  sandbox_->free_in_sandbox(sandboxed_version);
  DCHECK_EQ(Z_OK, err);
}

DeflateTransformer::~DeflateTransformer() {
  if (sandboxed_stream_) {
    if (!was_flush_called_) {
        sandbox_->invoke_sandbox_function(deflateEnd, sandboxed_stream_);
    }
    sandbox_->free_in_sandbox(sandboxed_stream_);
  }
  if (sandboxed_out_buffer_) {
    sandbox_->free_in_sandbox(sandboxed_out_buffer_);
  }
  if (sandboxed_in_buffer_) {
    sandbox_->free_in_sandbox(sandboxed_in_buffer_);
  }
}

ScriptPromise<IDLUndefined> DeflateTransformer::Transform(
    v8::Local<v8::Value> chunk,
    TransformStreamDefaultController* controller,
    ExceptionState& exception_state) {
  auto* buffer_source = V8BufferSource::Create(script_state_->GetIsolate(),
                                               chunk, exception_state);
  if (exception_state.HadException())
    return EmptyPromise();
  DOMArrayPiece array_piece(buffer_source);
  if (array_piece.ByteLength() > std::numeric_limits<wtf_size_t>::max()) {
    exception_state.ThrowRangeError(
        "Buffer size exceeds maximum heap object size.");
    return EmptyPromise();
  }
  Deflate(array_piece.Bytes(),
          static_cast<wtf_size_t>(array_piece.ByteLength()), IsFinished(false),
          controller, exception_state);
  return ToResolvedUndefinedPromise(script_state_.Get());
}

ScriptPromise<IDLUndefined> DeflateTransformer::Flush(
    TransformStreamDefaultController* controller,
    ExceptionState& exception_state) {
  Deflate(nullptr, 0u, IsFinished(true), controller, exception_state);
  was_flush_called_ = true;
  sandbox_->invoke_sandbox_function(deflateEnd, sandboxed_stream_);

  return ToResolvedUndefinedPromise(script_state_.Get());
}

void DeflateTransformer::Deflate(const uint8_t* start,
                                 wtf_size_t length,
                                 IsFinished finished,
                                 TransformStreamDefaultController* controller,
                                 ExceptionState& exception_state) {
  TRACE_EVENT("blink,devtools.timeline", "CompressionStream Deflate");

  // Allocate input buffer in sandbox and copy data if needed
  if (length > 0) {
    // Free old buffer if it exists
    if (sandboxed_in_buffer_) {
      sandbox_->free_in_sandbox(sandboxed_in_buffer_);
    }

    sandboxed_in_buffer_ = sandbox_->malloc_in_sandbox<uint8_t>(length);
    rlbox::memcpy(*sandbox_, sandboxed_in_buffer_, start, length);
  }

  // Set sandboxed stream input fields
  sandboxed_stream_->next_in = length > 0 ? sandboxed_in_buffer_ : nullptr;
  sandboxed_stream_->avail_in = length;

  // enqueue() may execute JavaScript which may invalidate the input buffer. So
  // accumulate all the output before calling enqueue().
  HeapVector<Member<DOMUint8Array>, 1u> buffers;

  uInt avail_out = 0;
  do {
    // Set sandboxed stream output fields
    sandboxed_stream_->avail_out = kBufferSize;
    sandboxed_stream_->next_out = sandboxed_out_buffer_;

    // Call deflate() in sandbox
    auto result = sandbox_->invoke_sandbox_function(
        deflate, sandboxed_stream_, finished ? Z_FINISH : Z_NO_FLUSH);
    const int err = result.unverified_safe_because(
        "Error code from deflate. We handle all zlib error codes safely.");
    DCHECK((finished && err == Z_STREAM_END) || err == Z_OK ||
           err == Z_BUF_ERROR);

    // Verify avail_out since we use it for bounds calculations
    avail_out = sandboxed_stream_->avail_out.copy_and_verify([](uInt val) {
      return val <= kBufferSize ? val : kBufferSize;
    });

    wtf_size_t bytes = kBufferSize - avail_out;

    if (bytes) {
      // Buffer overflow protection is provided by validating avail_out before
      // calculating bytes.
      auto verified_buffer = sandboxed_out_buffer_.unverified_safe_pointer_because(
          bytes,
          "Compressed data is inherently arbitrary and cannot be validated "
          "at this stage."
      );

      if (verified_buffer) {
        // Create DOMUint8Array and copy from sandbox in one step
        auto *dom_array = DOMUint8Array::Create(bytes);
        if (dom_array) {
          std::copy_n(verified_buffer, bytes, dom_array->Data());
          buffers.push_back(dom_array);
        } else {
          exception_state.ThrowTypeError("Failed to allocate compression buffer.");
          return;
        }
      } else {
        // Sandbox returned invalid buffer, something went wrong
        exception_state.ThrowTypeError("Internal compression error.");
        return;
      }
    }

  } while (avail_out == 0);

  auto remaining_in = sandboxed_stream_->avail_in.copy_and_verify([length](uInt val) {
    return val <= length ? val : 0u;
  });
  DCHECK_EQ(remaining_in, 0u);

  // JavaScript may be executed inside this loop, however it is safe because
  // |buffers| is a local variable that JavaScript cannot modify.
  for (DOMUint8Array* buffer : buffers) {
    controller->enqueue(script_state_, ScriptValue::From(script_state_, buffer),
                        exception_state);
    if (exception_state.HadException()) {
      return;
    }
  }
}

void DeflateTransformer::Trace(Visitor* visitor) const {
  visitor->Trace(script_state_);
  TransformStreamTransformer::Trace(visitor);
}

}  // namespace blink
