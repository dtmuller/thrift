/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <thrift/protocol/TJSONRPCProtocol.h>

#include <limits>
#include <locale>
#include <sstream>
#include <cmath>

#include <boost/math/special_functions/fpclassify.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>

#include <thrift/protocol/TBase64Utils.h>
#include <thrift/transport/TTransportException.h>

using namespace apache::thrift::transport;

namespace apache {
namespace thrift {
namespace protocol {

// Static data

static const std::string kJSONRPCName("jsonrpc");
static const std::string kJSONRPCVersion("2.0");
static const std::string kJSONRPCMethod("method");
static const std::string kJSONRPCParams("params");
static const std::string kJSONRPCId("id");
static const std::string kJSONRPCResult("result");
static const std::string kJSONRPCError("error");
static const std::string kJSONRPCCode("code");
static const std::string kJSONRPCMessage("message");
static const std::string kJSONRPCData("data");

TJSONRPCProtocol::TJSONRPCProtocol(boost::shared_ptr<TTransport> ptrans)
  : TVirtualProtocol<TJSONRPCProtocol>(ptrans),
    mode_(RW_TRANSPORT),
    flags_(JSONRPC_UNSET),
    buffer_(boost::shared_ptr<TMemoryBuffer>(new TMemoryBuffer())),
    transport_contexts_(ptrans),
    buffer_contexts_(buffer_) {
}

TJSONRPCProtocol::~TJSONRPCProtocol() {
}

/**
 * Writing functions
 */

uint32_t TJSONRPCProtocol::writeMessageBegin(const std::string& name,
                                          const TMessageType messageType,
                                          const int32_t seqid) {
  mode_ = RW_TRANSPORT;

  buffer_->resetBuffer();
  flags_ = JSONRPC_VERSION;

  uint32_t result = writeStructBegin("");
  result += writeString("jsonrpc");
  result += writeString(kJSONRPCVersion);

  switch (messageType) {
  case T_CALL:
    message_.method = name;
    message_.id = seqid;
    flags_ = JSONRPC_REQUEST;
    result += writeString(kJSONRPCMethod);
    result += writeString(message_.method);
    result += writeString(kJSONRPCParams);
    break;
  case T_ONEWAY:
    message_.method = name;
    flags_ = JSONRPC_NOTIFICATION;
    result += writeString(kJSONRPCMethod);
    result += writeString(message_.method);
    result += writeString(kJSONRPCParams);
    break;
  case T_REPLY:
    message_.id = seqid;
    flags_ = JSONRPC_RESPONSE;
    result += writeString(kJSONRPCResult);
    break;
  case T_EXCEPTION:
    message_.id = seqid;
    message_.error_code = -32000;
    message_.error_message = "Thrift exception";
    flags_ = JSONRPC_ERROR;
    result += writeString(kJSONRPCError);
    result += writeStructBegin("");
    result += writeString(kJSONRPCCode);
    result += writeI32(message_.error_code);
    result += writeString(kJSONRPCMessage);
    result += writeString(message_.error_message);
    // TODO do we always have the data field?
    result += writeString(kJSONRPCData);
    break;
  default:
    buffer_->resetBuffer();
    flags_ = JSONRPC_UNSET;
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED, "Unrecognized message type");
    break;
  }

  return result;
}

uint32_t TJSONRPCProtocol::writeMessageEnd() {
  uint32_t result = 0;
  switch (flags_) {
    case JSONRPC_REQUEST:
    case JSONRPC_FULL_REQUEST:
    case JSONRPC_RESPONSE:
      result += writeString(kJSONRPCId);
      result += writeI32(message_.id);
      break;
    case JSONRPC_ERROR:
    case JSONRPC_FULL_ERROR:
      result += writeStructEnd();
      result += writeString(kJSONRPCId);
      result += writeI32(message_.id);
      break;
    case JSONRPC_NOTIFICATION:
      break;
    default:
      buffer_->resetBuffer();
      flags_ = JSONRPC_UNSET;
      throw TProtocolException(TProtocolException::INVALID_DATA, "Invalid JSONRPC message");
      break;
  }
  result += writeStructEnd();
  buffer_->resetBuffer();
  flags_ = JSONRPC_UNSET;
  return 0;
}

uint32_t TJSONRPCProtocol::writeStructBegin(const char* name) {
  (void)name;
  return contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
}

uint32_t TJSONRPCProtocol::writeStructEnd() {
  return contexts().popWrite();
}

uint32_t TJSONRPCProtocol::writeFieldBegin(const char* name, const TType fieldType, const int16_t fieldId) {
  (void)name;
  // TODO add the "p"-prefix here
  uint32_t result = context()->writeInteger(fieldId);
  result += contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
  result += context()->writeTypeID(fieldType);
  return result;
}

uint32_t TJSONRPCProtocol::writeFieldEnd() {
  return contexts().popWrite();
}

uint32_t TJSONRPCProtocol::writeFieldStop() {
  return 0;
}

uint32_t TJSONRPCProtocol::writeMapBegin(const TType keyType, const TType valType, const uint32_t size) {
  uint32_t result = contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->writeTypeID(keyType);
  result += context()->writeTypeID(valType);
  result += context()->writeInteger((int64_t)size);
  result += contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
  return result;
}

uint32_t TJSONRPCProtocol::writeMapEnd() {
  uint32_t result = contexts().popWrite();
  result += contexts().popWrite();
  return result;
}

uint32_t TJSONRPCProtocol::writeListBegin(const TType elemType, const uint32_t size) {
  uint32_t result = contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->writeTypeID(elemType);
  result += context()->writeInteger((int64_t)size);
  return result;
}

uint32_t TJSONRPCProtocol::writeListEnd() {
  return contexts().popWrite();
}

uint32_t TJSONRPCProtocol::writeSetBegin(const TType elemType, const uint32_t size) {
  uint32_t result = contexts().pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->writeTypeID(elemType);
  result += context()->writeInteger((int64_t)size);
  return result;
}

uint32_t TJSONRPCProtocol::writeSetEnd() {
  return contexts().popWrite();
}

uint32_t TJSONRPCProtocol::writeBool(const bool value) {
  return context()->writeInteger(value);
}

uint32_t TJSONRPCProtocol::writeByte(const int8_t byte) {
  // writeByte() must be handled specially because boost::lexical cast sees
  // int8_t as a text type instead of an integer type
  return context()->writeInteger((int16_t)byte);
}

uint32_t TJSONRPCProtocol::writeI16(const int16_t i16) {
  return context()->writeInteger(i16);
}

uint32_t TJSONRPCProtocol::writeI32(const int32_t i32) {
  return context()->writeInteger(i32);
}

uint32_t TJSONRPCProtocol::writeI64(const int64_t i64) {
  return context()->writeInteger(i64);
}

uint32_t TJSONRPCProtocol::writeDouble(const double dub) {
  return context()->writeDouble(dub);
}

uint32_t TJSONRPCProtocol::writeString(const std::string& str) {
  return context()->writeString(str);
}

uint32_t TJSONRPCProtocol::writeBinary(const std::string& str) {
  return context()->writeBase64(str);
}

/**
 * Reading functions
 */

uint32_t TJSONRPCProtocol::readMessageBegin(std::string& name,
                                            TMessageType& messageType,
                                            int32_t& seqid) {
  buffer_->resetBuffer();
  mode_ = RW_TRANSPORT;
  flags_ = JSONRPC_UNSET;

  // TODO Add support for JSONRPC arrays?!

  // Need to parse complete JSONRPC message because JSON objects are unordered
  // and we don't know what's coming.
  std::string tmp;
  uint32_t result = readStructBegin(tmp);
  while (true) {
    uint8_t ch = context()->peek();
    if (ch == kJSONObjectEnd) {
      break;
    }
    result += readJSONRPCField();
  }
  result += readStructEnd();

  switch (flags_) {
    case JSONRPC_REQUEST:
    case JSONRPC_FULL_REQUEST:
      name = message_.method;
      messageType = T_CALL;
      seqid = message_.id;
      // JSON context needs an empty object if params left out.
      if (flags_ == JSONRPC_REQUEST){
          buffer_->write(&kJSONObjectStart, 1);
          buffer_->write(&kJSONObjectEnd, 1);
      }
      break;
    case JSONRPC_NOTIFICATION:
    case JSONRPC_FULL_NOTIFICATION:
      name = message_.method;
      messageType = T_ONEWAY;
      seqid = 0;  // dummy ID for oneway methods
      // JSON context needs an empty object if params left out.
      if (flags_ == JSONRPC_NOTIFICATION){
          buffer_->write(&kJSONObjectStart, 1);
          buffer_->write(&kJSONObjectEnd, 1);
      }
      break;
    case JSONRPC_ERROR:
    case JSONRPC_FULL_ERROR:
    case JSONRPC_RESPONSE:
      // Response must be matched solely by seqid as JSONRPC response doesn't
      // include the method name.
      name = "";
      messageType = (flags_ == JSONRPC_RESPONSE) ? T_REPLY : T_EXCEPTION;
      seqid = message_.id;
      // JSON context needs an empty object if data left out.
      if (flags_ == JSONRPC_ERROR){
          buffer_->write(&kJSONObjectStart, 1);
          buffer_->write(&kJSONObjectEnd, 1);
      }
      break;
    default:
      buffer_->resetBuffer();
      flags_ = JSONRPC_UNSET;
      throw TProtocolException(TProtocolException::INVALID_DATA, "Invalid JSONRPC message");
      break;
  }

  mode_ = RW_BUFFERED;
  return result;
}

uint32_t TJSONRPCProtocol::readMessageEnd() {
  buffer_->resetBuffer();
  flags_ = JSONRPC_UNSET;
  mode_ = RW_TRANSPORT;
  return 0;
}

uint32_t TJSONRPCProtocol::readStructBegin(std::string& name) {
  (void)name;
  return contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
}

uint32_t TJSONRPCProtocol::readStructEnd() {
  return contexts().popRead();
}

uint32_t TJSONRPCProtocol::readFieldBegin(std::string& name, TType& fieldType, int16_t& fieldId) {
  (void)name;
  uint32_t result = 0;
  // Check if we hit the end of the list
  uint8_t ch = context()->peek();
  if (ch == kJSONObjectEnd) {
    fieldType = apache::thrift::protocol::T_STOP;
  } else {
    uint64_t tmpVal = 0;
    result += context()->readInteger(tmpVal);
    if (tmpVal > static_cast<uint32_t>((std::numeric_limits<int16_t>::max)()))
      throw TProtocolException(TProtocolException::SIZE_LIMIT);
    fieldId = static_cast<int16_t>(tmpVal);
    result += contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
    result += context()->readTypeID(fieldType);
  }
  return result;
}

uint32_t TJSONRPCProtocol::readFieldEnd() {
  return contexts().popRead();
}

uint32_t TJSONRPCProtocol::readMapBegin(TType& keyType, TType& valType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->readTypeID(keyType);
  result += context()->readTypeID(valType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  result += contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(transport())));
  return result;
}

uint32_t TJSONRPCProtocol::readMapEnd() {
  uint32_t result = contexts().popRead();
  result += contexts().popRead();
  return result;
}

uint32_t TJSONRPCProtocol::readListBegin(TType& elemType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->readTypeID(elemType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  return result;
}

uint32_t TJSONRPCProtocol::readListEnd() {
  return contexts().popRead();
}

uint32_t TJSONRPCProtocol::readSetBegin(TType& elemType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts().pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(transport())));
  result += context()->readTypeID(elemType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  return result;
}

uint32_t TJSONRPCProtocol::readSetEnd() {
  return contexts().popRead();
}

uint32_t TJSONRPCProtocol::readBool(bool& value) {
  return context()->readInteger(value);
}

uint32_t TJSONRPCProtocol::readByte(int8_t& byte) {
  int16_t tmp = (int16_t)byte;
  uint32_t result = context()->readInteger(tmp);
  assert(tmp < 256);
  byte = (int8_t)tmp;
  return result;
}

uint32_t TJSONRPCProtocol::readI16(int16_t& i16) {
  return context()->readInteger(i16);
}

uint32_t TJSONRPCProtocol::readI32(int32_t& i32) {
  return context()->readInteger(i32);
}

uint32_t TJSONRPCProtocol::readI64(int64_t& i64) {
  return context()->readInteger(i64);
}

uint32_t TJSONRPCProtocol::readDouble(double& dub) {
  return context()->readDouble(dub);
}

uint32_t TJSONRPCProtocol::readString(std::string& str) {
  return context()->readString(str);
}

uint32_t TJSONRPCProtocol::readBinary(std::string& str) {
  return context()->readBase64(str);
}

uint32_t TJSONRPCProtocol::readJSONRPCField() {
  std::string tmpString;
  uint32_t result = readString(tmpString);
  if (tmpString == kJSONRPCName) {
    result += readString(tmpString);
    if (tmpString != kJSONRPCVersion) {
      throw TProtocolException(TProtocolException::BAD_VERSION, "Message contained bad version.");
    }
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_VERSION;
    flags_ = operator|(flags_, JSONRPC_VERSION);
  }
  else if (tmpString == kJSONRPCMethod) {
    result += readString(message_.method);
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_METHOD;
    flags_ = operator|(flags_, JSONRPC_METHOD);
  }
  else if (tmpString == kJSONRPCId) {
    result += readI32(message_.id);
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_ID;
    flags_ = operator|(flags_, JSONRPC_ID);
  }
  else if (tmpString == kJSONRPCParams) {
    result += context()->readObject(*buffer_.get());
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_PARAMS;
    flags_ = operator|(flags_, JSONRPC_PARAMS);
  }
  else if (tmpString == kJSONRPCResult) {
    // TODO not allowed if error is set
    result += context()->readObject(*buffer_.get());
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_RESULT;
    flags_ = operator|(flags_, JSONRPC_RESULT);
  }
  else if (tmpString == kJSONRPCError) {
    std::string tmp;
    result += readStructBegin(tmp);
    // Recursively parse nested error fields: code, message, data.
    do {
      result += readJSONRPCField();
    } while (context()->peek() != kJSONObjectEnd);
    result += readStructEnd();
  }
  else if (tmpString == kJSONRPCCode) {
    result += readI32(message_.error_code);
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_ERR_CODE;
    flags_ = operator|(flags_, JSONRPC_ERR_CODE);
  }
  else if (tmpString == kJSONRPCMessage) {
    result += readString(message_.error_message);
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_ERR_MSG;
    flags_ = operator|(flags_, JSONRPC_ERR_MSG);
  }
  else if (tmpString == kJSONRPCData) {
    // TODO keep in mind that data is optional
    result += context()->readObject(*buffer_.get());
    // FIXME operator| doesn't work
    //flags_ |= JSONRPC_ERR_DATA;
    flags_ = operator|(flags_, JSONRPC_ERR_DATA);
  }
  else {
    throw TProtocolException(TProtocolException::INVALID_DATA, "Unknown JSONRPC keyword: " + tmpString);
  }
  return result;
}

}
}
} // apache::thrift::protocol
