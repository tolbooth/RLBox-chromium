// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_DEFLATE_TRANSFORMER_H_
#define THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_DEFLATE_TRANSFORMER_H_

#include "base/memory/raw_ptr.h"
#include "base/types/strong_alias.h"
#include "third_party/blink/renderer/core/streams/transform_stream_transformer.h"
#include "third_party/blink/renderer/core/typed_arrays/dom_typed_array.h"
#include "third_party/blink/renderer/modules/compression/zlib_rlbox_types.h"
#include "third_party/blink/renderer/platform/wtf/vector.h"
#include "third_party/zlib/zlib.h"

namespace blink {

enum class CompressionFormat;

class DeflateTransformer final : public TransformStreamTransformer {
 public:
  DeflateTransformer(ScriptState*,
                     CompressionFormat,
                     int level,
                     rlbox_sandbox_zlib* sandbox);

  DeflateTransformer(const DeflateTransformer&) = delete;
  DeflateTransformer& operator=(const DeflateTransformer&) = delete;

  ~DeflateTransformer() override;

  ScriptPromise<IDLUndefined> Transform(v8::Local<v8::Value> chunk,
                                        TransformStreamDefaultController*,
                                        ExceptionState&) override;

  ScriptPromise<IDLUndefined> Flush(TransformStreamDefaultController*,
                                    ExceptionState&) override;

  ScriptState* GetScriptState() override { return script_state_.Get(); }

  void Trace(Visitor*) const override;

 private:
  using IsFinished = base::StrongAlias<class IsFinishedTag, bool>;

  void Deflate(const uint8_t*,
               wtf_size_t,
               IsFinished,
               TransformStreamDefaultController*,
               ExceptionState&);

  Member<ScriptState> script_state_;

  // RLBox sandbox pointer (owned by CompressionStream)
  raw_ptr<rlbox_sandbox_zlib> sandbox_;

  // Sandboxed z_stream structure
  tainted_zlib<z_stream*> sandboxed_stream_;

  // Sandboxed buffers for input and output
  tainted_zlib<uint8_t*> sandboxed_out_buffer_;
  tainted_zlib<uint8_t*> sandboxed_in_buffer_;

  bool was_flush_called_ = false;

  // This buffer size has been experimentally verified to be optimal.
  static constexpr wtf_size_t kBufferSize = 16384;
};

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_MODULES_COMPRESSION_DEFLATE_TRANSFORMER_H_
