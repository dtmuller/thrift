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

#include <thrift/protocol/TJSONProtocol.h>

#include <limits>
#include <locale>
#include <sstream>
#include <cmath>

#include <boost/math/special_functions/fpclassify.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>

#include <thrift/protocol/TJSONUtils.h>
#include <thrift/transport/TTransportException.h>

using namespace apache::thrift::transport;

namespace apache {
namespace thrift {
namespace protocol {

// Static data

static const uint32_t kThriftVersion1 = 1;

TJSONProtocol::TJSONProtocol(boost::shared_ptr<TTransport> ptrans)
  : TVirtualProtocol<TJSONProtocol>(ptrans),
    trans_(ptrans.get()),
    contexts_(ptrans) {
}

TJSONProtocol::~TJSONProtocol() {
}

uint32_t TJSONProtocol::writeMessageBegin(const std::string& name,
                                          const TMessageType messageType,
                                          const int32_t seqid) {
  uint32_t result = contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->writeInteger(kThriftVersion1);
  result += context()->writeString(name);
  result += context()->writeInteger(messageType);
  result += context()->writeInteger(seqid);
  return result;
}

uint32_t TJSONProtocol::writeMessageEnd() {
  return contexts_.popWrite();
}

uint32_t TJSONProtocol::writeStructBegin(const char* name) {
  (void)name;
  return contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
}

uint32_t TJSONProtocol::writeStructEnd() {
  return contexts_.popWrite();
}

uint32_t TJSONProtocol::writeFieldBegin(const char* name,
                                        const TType fieldType,
                                        const int16_t fieldId) {
  (void)name;
  uint32_t result = context()->writeInteger(fieldId);
  result += contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
  result += context()->writeTypeID(fieldType);
  return result;
}

uint32_t TJSONProtocol::writeFieldEnd() {
  return contexts_.popWrite();
}

uint32_t TJSONProtocol::writeFieldStop() {
  return 0;
}

uint32_t TJSONProtocol::writeMapBegin(const TType keyType,
                                      const TType valType,
                                      const uint32_t size) {
  uint32_t result = contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->writeTypeID(keyType);
  result += context()->writeTypeID(valType);
  result += context()->writeInteger((int64_t)size);
  result += contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
  return result;
}

uint32_t TJSONProtocol::writeMapEnd() {
  uint32_t result = contexts_.popWrite();
  result += contexts_.popWrite();
  return result;
}

uint32_t TJSONProtocol::writeListBegin(const TType elemType, const uint32_t size) {
  uint32_t result = contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->writeTypeID(elemType);
  result += context()->writeInteger((int64_t)size);
  return result;
}

uint32_t TJSONProtocol::writeListEnd() {
  return contexts_.popWrite();
}

uint32_t TJSONProtocol::writeSetBegin(const TType elemType, const uint32_t size) {
  uint32_t result = contexts_.pushWrite(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->writeTypeID(elemType);
  result += context()->writeInteger((int64_t)size);
  return result;
}

uint32_t TJSONProtocol::writeSetEnd() {
  return contexts_.popWrite();
}

uint32_t TJSONProtocol::writeBool(const bool value) {
  return context()->writeInteger(value);
}

uint32_t TJSONProtocol::writeByte(const int8_t byte) {
  // writeByte() must be handled specially because boost::lexical cast sees
  // int8_t as a text type instead of an integer type
  return context()->writeInteger((int16_t)byte);
}

uint32_t TJSONProtocol::writeI16(const int16_t i16) {
  return context()->writeInteger(i16);
}

uint32_t TJSONProtocol::writeI32(const int32_t i32) {
  return context()->writeInteger(i32);
}

uint32_t TJSONProtocol::writeI64(const int64_t i64) {
  return context()->writeInteger(i64);
}

uint32_t TJSONProtocol::writeDouble(const double dub) {
  return context()->writeDouble(dub);
}

uint32_t TJSONProtocol::writeString(const std::string& str) {
  return context()->writeString(str);
}

uint32_t TJSONProtocol::writeBinary(const std::string& str) {
  return context()->writeBase64(str);
}

/**
 * Reading functions
 */

uint32_t TJSONProtocol::readMessageBegin(std::string& name,
                                         TMessageType& messageType,
                                         int32_t& seqid) {
  uint32_t result = contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  uint64_t tmpVal = 0;
  result += context()->readInteger(tmpVal);
  if (tmpVal != kThriftVersion1) {
    throw TProtocolException(TProtocolException::BAD_VERSION, "Message contained bad version.");
  }
  result += context()->readString(name);
  result += context()->readInteger(tmpVal);
  messageType = (TMessageType)tmpVal;
  result += context()->readInteger(tmpVal);
  if (tmpVal > static_cast<uint64_t>((std::numeric_limits<int32_t>::max)()))
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  seqid = static_cast<int32_t>(tmpVal);
  return result;
}

uint32_t TJSONProtocol::readMessageEnd() {
  return contexts_.popRead();
}

uint32_t TJSONProtocol::readStructBegin(std::string& name) {
  (void)name;
  return contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
}

uint32_t TJSONProtocol::readStructEnd() {
  return contexts_.popRead();
}

uint32_t TJSONProtocol::readFieldBegin(std::string& name, TType& fieldType, int16_t& fieldId) {
  (void)name;
  uint32_t result = 0;
  // Check if we hit the end of the list
  uint8_t ch = context()->peek();
  if (ch == kJSONObjectEnd) {
    fieldType = apache::thrift::protocol::T_STOP;
  } else {
    uint64_t tmpVal = 0;
    std::string tmpStr;
    result += context()->readInteger(tmpVal);
    if (tmpVal > static_cast<uint32_t>((std::numeric_limits<int16_t>::max)()))
      throw TProtocolException(TProtocolException::SIZE_LIMIT);
    fieldId = static_cast<int16_t>(tmpVal);
    result += contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
    result += context()->readTypeID(fieldType);
  }
  return result;
}

uint32_t TJSONProtocol::readFieldEnd() {
  return contexts_.popRead();
}

uint32_t TJSONProtocol::readMapBegin(TType& keyType, TType& valType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->readTypeID(keyType);
  result += context()->readTypeID(valType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  result += contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONPairContext(ptrans_)));
  return result;
}

uint32_t TJSONProtocol::readMapEnd() {
  uint32_t result = contexts_.popRead();
  result += contexts_.popRead();
  return result;
}

uint32_t TJSONProtocol::readListBegin(TType& elemType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->readTypeID(elemType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  return result;
}

uint32_t TJSONProtocol::readListEnd() {
  return contexts_.popRead();
}

uint32_t TJSONProtocol::readSetBegin(TType& elemType, uint32_t& size) {
  uint64_t tmpVal = 0;
  uint32_t result = contexts_.pushRead(boost::shared_ptr<TJSONContext>(new TJSONListContext(ptrans_)));
  result += context()->readTypeID(elemType);
  result += context()->readInteger(tmpVal);
  if (tmpVal > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  size = static_cast<uint32_t>(tmpVal);
  return result;
}

uint32_t TJSONProtocol::readSetEnd() {
  return contexts_.popRead();
}

uint32_t TJSONProtocol::readBool(bool& value) {
  return context()->readInteger(value);
}

// readByte() must be handled properly because boost::lexical cast sees int8_t
// as a text type instead of an integer type
uint32_t TJSONProtocol::readByte(int8_t& byte) {
  int16_t tmp = (int16_t)byte;
  uint32_t result = context()->readInteger(tmp);
  assert(tmp < 256);
  byte = (int8_t)tmp;
  return result;
}

uint32_t TJSONProtocol::readI16(int16_t& i16) {
  return context()->readInteger(i16);
}

uint32_t TJSONProtocol::readI32(int32_t& i32) {
  return context()->readInteger(i32);
}

uint32_t TJSONProtocol::readI64(int64_t& i64) {
  return context()->readInteger(i64);
}

uint32_t TJSONProtocol::readDouble(double& dub) {
  return context()->readDouble(dub);
}

uint32_t TJSONProtocol::readString(std::string& str) {
  return context()->readString(str);
}

uint32_t TJSONProtocol::readBinary(std::string& str) {
  return context()->readBase64(str);
}

}
}
} // apache::thrift::protocol
