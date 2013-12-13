// Parts of this file are:

// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


#include "arraybuffers.h"

#include <assert.h>
#include <string.h>

#ifndef ASSERT
#define ASSERT(condition) assert(condition)
#endif


using namespace v8;


const char kArrayBufferMarkerPropName[] = "d8::_is_array_buffer_";


static size_t convertToUint(Local<Value> value_in, TryCatch* try_catch) {
  if (value_in->IsUint32()) {
    return value_in->Uint32Value();
  }

  Local<Value> number = value_in->ToNumber();
  if (try_catch->HasCaught()) return 0;

  ASSERT(number->IsNumber());
  Local<Int32> int32 = number->ToInt32();
  if (try_catch->HasCaught() || int32.IsEmpty()) return 0;

  int32_t raw_value = int32->Int32Value();
  if (try_catch->HasCaught()) return 0;

  if (raw_value < 0) {
    ThrowException(String::New("Array length must not be negative."));
    return 0;
  }

  static const int kMaxLength = 0x3fffffff;
  if (raw_value > static_cast<int32_t>(kMaxLength)) {
    ThrowException(
        String::New("Array length exceeds maximum length."));
  }
  return static_cast<size_t>(raw_value);
}

void ExternalArrayWeakCallback(Persistent<Value> object, void* data) {
  HandleScope scope;
  int32_t length =
      object->ToObject()->Get(String::New("byteLength"))->Uint32Value();
  V8::AdjustAmountOfExternalAllocatedMemory(-length);
  delete[] static_cast<uint8_t*>(data);
  object.Dispose();
}

Handle<Value> CreateExternalArrayBuffer(int32_t length) {
  static const int32_t kMaxSize = 0x7fffffff;
  // Make sure the total size fits into a (signed) int.
  if (length < 0 || length > kMaxSize) {
    return ThrowException(String::New("ArrayBuffer exceeds maximum size (2G)"));
  }
  uint8_t* data = new uint8_t[length];
  if (data == NULL) {
    return ThrowException(String::New("Memory allocation failed."));
  }
  memset(data, 0, length);

  Handle<Object> buffer = Object::New();
  buffer->SetHiddenValue(String::New(kArrayBufferMarkerPropName), True());
  Persistent<Object> persistent_array = Persistent<Object>::New(buffer);
  persistent_array.MakeWeak(data, ExternalArrayWeakCallback);
  persistent_array.MarkIndependent();
  V8::AdjustAmountOfExternalAllocatedMemory(length);

  buffer->SetIndexedPropertiesToExternalArrayData(
      data, v8::kExternalByteArray, length);
  buffer->Set(String::New("byteLength"), Int32::New(length), ReadOnly);

  return buffer;
}


Handle<Value> CreateExternalArrayBuffer(const Arguments& args) {
  if (args.Length() == 0) {
    return ThrowException(
        String::New("ArrayBuffer constructor must have one parameter."));
  }
  TryCatch try_catch;
  int32_t length = convertToUint(args[0], &try_catch);
  if (try_catch.HasCaught()) return try_catch.Exception();

  return CreateExternalArrayBuffer(length);
}


Handle<Value> CreateExternalArray(const Arguments& args,
                                         ExternalArrayType type,
                                         int32_t element_size) {
  TryCatch try_catch;
  ASSERT(element_size == 1 || element_size == 2 ||
         element_size == 4 || element_size == 8);

  // Currently, only the following constructors are supported:
  //   TypedArray(unsigned long length)
  //   TypedArray(ArrayBuffer buffer,
  //              optional unsigned long byteOffset,
  //              optional unsigned long length)
  Handle<Object> buffer;
  int32_t length;
  int32_t byteLength;
  int32_t byteOffset;
  if (args.Length() == 0) {
    return ThrowException(
        String::New("Array constructor must have at least one parameter."));
  }
  if (args[0]->IsObject() &&
     !args[0]->ToObject()->GetHiddenValue(
         String::New(kArrayBufferMarkerPropName)).IsEmpty()) {
    buffer = args[0]->ToObject();
    int32_t bufferLength =
        convertToUint(buffer->Get(String::New("byteLength")), &try_catch);
    if (try_catch.HasCaught()) return try_catch.Exception();

    if (args.Length() < 2 || args[1]->IsUndefined()) {
      byteOffset = 0;
    } else {
      byteOffset = convertToUint(args[1], &try_catch);
      if (try_catch.HasCaught()) return try_catch.Exception();
      if (byteOffset > bufferLength) {
        return ThrowException(String::New("byteOffset out of bounds"));
      }
      if (byteOffset % element_size != 0) {
        return ThrowException(
            String::New("byteOffset must be multiple of element_size"));
      }
    }

    if (args.Length() < 3 || args[2]->IsUndefined()) {
      byteLength = bufferLength - byteOffset;
      length = byteLength / element_size;
      if (byteLength % element_size != 0) {
        return ThrowException(
            String::New("buffer size must be multiple of element_size"));
      }
    } else {
      length = convertToUint(args[2], &try_catch);
      if (try_catch.HasCaught()) return try_catch.Exception();
      byteLength = length * element_size;
      if (byteOffset + byteLength > bufferLength) {
        return ThrowException(String::New("length out of bounds"));
      }
    }
  } else {
    length = convertToUint(args[0], &try_catch);
    byteLength = length * element_size;
    byteOffset = 0;
    Handle<Value> result = CreateExternalArrayBuffer(byteLength);
    if (!result->IsObject()) return result;
    buffer = result->ToObject();
  }

  void* data = buffer->GetIndexedPropertiesExternalArrayData();
  ASSERT(data != NULL);

  Handle<Object> array = Object::New();
  array->SetIndexedPropertiesToExternalArrayData(
      static_cast<uint8_t*>(data) + byteOffset, type, length);
  array->Set(String::New("byteLength"), Int32::New(byteLength), ReadOnly);
  array->Set(String::New("byteOffset"), Int32::New(byteOffset), ReadOnly);
  array->Set(String::New("length"), Int32::New(length), ReadOnly);
  array->Set(String::New("BYTES_PER_ELEMENT"), Int32::New(element_size));
  array->Set(String::New("buffer"), buffer, ReadOnly);

  return array;
}



Handle<Value> ArrayBuffers::ArrayBuffer(const Arguments& args) {
  return CreateExternalArrayBuffer(args);
}


Handle<Value> ArrayBuffers::Int8Array(const Arguments& args) {
  return CreateExternalArray(args, v8::kExternalByteArray, sizeof(int8_t));
}


Handle<Value> ArrayBuffers::Uint8Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalUnsignedByteArray, sizeof(uint8_t));
}


Handle<Value> ArrayBuffers::Int16Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalShortArray, sizeof(int16_t));
}


Handle<Value> ArrayBuffers::Uint16Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalUnsignedShortArray,
                             sizeof(uint16_t));
}


Handle<Value> ArrayBuffers::Int32Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalIntArray, sizeof(int32_t));
}


Handle<Value> ArrayBuffers::Uint32Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalUnsignedIntArray, sizeof(uint32_t));
}


Handle<Value> ArrayBuffers::Float32Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalFloatArray,
                             sizeof(float));  // NOLINT
}


Handle<Value> ArrayBuffers::Float64Array(const Arguments& args) {
  return CreateExternalArray(args, kExternalDoubleArray,
                             sizeof(double));  // NOLINT
}