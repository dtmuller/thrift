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

#include <limits>
#include <locale>
#include <cmath>

#include <boost/lexical_cast.hpp>
#include <boost/locale.hpp>

#include <thrift/protocol/TBase64Utils.h>
#include <thrift/transport/TTransportException.h>
#include <thrift/protocol/TJSONUtils.h>

using namespace apache::thrift::transport;

namespace apache {
namespace thrift {
namespace protocol {

// Static data

static const std::string kJSONEscapePrefix("\\u00");

static const std::string kThriftNan("NaN");
static const std::string kThriftInfinity("Infinity");
static const std::string kThriftNegativeInfinity("-Infinity");

// This table describes the handling for the first 0x30 characters
//  0 : escape using "\u00xx" notation
//  1 : just output index
// <other> : escape using "\<other>" notation
static const uint8_t kJSONCharTable[0x30] = {
    //  0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    'b',
    't',
    'n',
    0,
    'f',
    'r',
    0,
    0, // 0
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 1
    1,
    1,
    '"',
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1, // 2
};

// This string's characters must match up with the elements in kEscapeCharVals.
// I don't have '/' on this list even though it appears on www.json.org --
// it is not in the RFC
const static std::string kEscapeChars("\"\\bfnrt");

// The elements of this array must match up with the sequence of characters in
// kEscapeChars
const static uint8_t kEscapeCharVals[7] = {
    '"',
    '\\',
    '\b',
    '\f',
    '\n',
    '\r',
    '\t',
};

static const std::string kTypeNameBool("tf");
static const std::string kTypeNameByte("i8");
static const std::string kTypeNameI16("i16");
static const std::string kTypeNameI32("i32");
static const std::string kTypeNameI64("i64");
static const std::string kTypeNameDouble("dbl");
static const std::string kTypeNameStruct("rec");
static const std::string kTypeNameString("str");
static const std::string kTypeNameMap("map");
static const std::string kTypeNameList("lst");
static const std::string kTypeNameSet("set");

// Static helper functions

static const std::string& getTypeNameForTypeID(TType typeID) {
  switch (typeID) {
  case T_BOOL:
    return kTypeNameBool;
  case T_BYTE:
    return kTypeNameByte;
  case T_I16:
    return kTypeNameI16;
  case T_I32:
    return kTypeNameI32;
  case T_I64:
    return kTypeNameI64;
  case T_DOUBLE:
    return kTypeNameDouble;
  case T_STRING:
    return kTypeNameString;
  case T_STRUCT:
    return kTypeNameStruct;
  case T_MAP:
    return kTypeNameMap;
  case T_SET:
    return kTypeNameSet;
  case T_LIST:
    return kTypeNameList;
  default:
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED, "Unrecognized type");
  }
}

static TType getTypeIDForTypeName(const std::string& name) {
  TType result = T_STOP; // Sentinel value
  if (name.length() > 1) {
    switch (name[0]) {
    case 'd':
      result = T_DOUBLE;
      break;
    case 'i':
      switch (name[1]) {
      case '8':
        result = T_BYTE;
        break;
      case '1':
        result = T_I16;
        break;
      case '3':
        result = T_I32;
        break;
      case '6':
        result = T_I64;
        break;
      }
      break;
    case 'l':
      result = T_LIST;
      break;
    case 'm':
      result = T_MAP;
      break;
    case 'r':
      result = T_STRUCT;
      break;
    case 's':
      if (name[1] == 't') {
        result = T_STRING;
      } else if (name[1] == 'e') {
        result = T_SET;
      }
      break;
    case 't':
      result = T_BOOL;
      break;
    }
  }
  if (result == T_STOP) {
    throw TProtocolException(TProtocolException::NOT_IMPLEMENTED, "Unrecognized type");
  }
  return result;
}

// Read 1 character from the transport trans and verify that it is the
// expected character ch.
// Throw a protocol exception if it is not.
static uint32_t readSyntaxChar(TJSONContext::LookaheadReader& reader, uint8_t ch) {
  uint8_t ch2 = reader.read();
  if (ch2 != ch) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "Expected \'" + std::string((char*)&ch, 1) + "\'; got \'"
                             + std::string((char*)&ch2, 1) + "\'.");
  }
  return 1;
}

// Return the integer value of a hex character ch.
// Throw a protocol exception if the character is not [0-9a-f].
static uint8_t hexVal(uint8_t ch) {
  if ((ch >= '0') && (ch <= '9')) {
    return ch - '0';
  } else if ((ch >= 'a') && (ch <= 'f')) {
    return ch - 'a' + 10;
  } else {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "Expected hex val ([0-9a-f]); got \'" + std::string((char*)&ch, 1)
                             + "\'.");
  }
}

// Return the hex character representing the integer val. The value is masked
// to make sure it is in the correct range.
static uint8_t hexChar(uint8_t val) {
  val &= 0x0F;
  if (val < 10) {
    return val + '0';
  } else {
    return val - 10 + 'a';
  }
}

// Return true if the character ch is in [-+0-9.Ee]; false otherwise
static bool isJSONNumeric(uint8_t ch) {
  switch (ch) {
  case '+':
  case '-':
  case '.':
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case 'E':
  case 'e':
    return true;
  }
  return false;
}

// Return true if the code unit is high surrogate
static bool isHighSurrogate(uint16_t val) {
  return val >= 0xD800 && val <= 0xDBFF;
}

// Return true if the code unit is low surrogate
static bool isLowSurrogate(uint16_t val) {
  return val >= 0xDC00 && val <= 0xDFFF;
}



TJSONContext::TJSONContext(boost::shared_ptr<TTransport> trans)
  : trans_(trans),
    reader_(trans) {
}

TJSONContext::~TJSONContext() {
}

boost::shared_ptr<TTransport> TJSONContext::transport() {
  return trans_;
}

TJSONContext::LookaheadReader& TJSONContext::reader() {
  return reader_;
}

uint32_t TJSONContext::writeNext() {
  return 0;
}

uint32_t TJSONContext::readNext() {
  return 0;
}

bool TJSONContext::escapeNum() {
  return false;
}

uint32_t TJSONContext::writeStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->writeNext();
  transport()->write(&kJSONObjectStart, 1);
  return result + 1;
}

uint32_t TJSONContext::writeEnd() {
  transport()->write(&kJSONObjectEnd, 1);
  return 1;
}

uint32_t TJSONContext::readStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->readNext();
  result += readSyntaxChar(reader(), kJSONObjectStart);
  return result;
}

uint32_t TJSONContext::readEnd() {
  return readSyntaxChar(reader(), kJSONObjectEnd);
}

// Write the character ch as a JSON escape sequence ("\u00xx")
uint32_t TJSONContext::writeEscapeChar(uint8_t ch) {
  trans_->write((const uint8_t*)kJSONEscapePrefix.c_str(),
                static_cast<uint32_t>(kJSONEscapePrefix.length()));
  uint8_t outCh = hexChar(ch >> 4);
  trans_->write(&outCh, 1);
  outCh = hexChar(ch);
  trans_->write(&outCh, 1);
  return 6;
}

// Write the character ch as part of a JSON string, escaping as appropriate.
uint32_t TJSONContext::writeChar(uint8_t ch) {
  if (ch >= 0x30) {
    if (ch == kJSONBackslash) { // Only special character >= 0x30 is '\'
      trans_->write(&kJSONBackslash, 1);
      trans_->write(&kJSONBackslash, 1);
      return 2;
    } else {
      trans_->write(&ch, 1);
      return 1;
    }
  } else {
    uint8_t outCh = kJSONCharTable[ch];
    // Check if regular character, backslash escaped, or JSON escaped
    if (outCh == 1) {
      trans_->write(&ch, 1);
      return 1;
    } else if (outCh > 1) {
      trans_->write(&kJSONBackslash, 1);
      trans_->write(&outCh, 1);
      return 2;
    } else {
      return writeEscapeChar(ch);
    }
  }
}

uint32_t TJSONContext::writeTypeID(TType typeID) {
  return writeString(getTypeNameForTypeID(typeID));
}

// Write out the contents of the string str as a JSON string, escaping
// characters as appropriate.
uint32_t TJSONContext::writeString(const std::string& str) {
  uint32_t result = writeNext();
  result += 2; // For quotes
  trans_->write(&kJSONStringDelimiter, 1);
  std::string::const_iterator iter(str.begin());
  std::string::const_iterator end(str.end());
  while (iter != end) {
    result += writeChar(*iter++);
  }
  trans_->write(&kJSONStringDelimiter, 1);
  return result;
}

// Write out the contents of the string as JSON string, base64-encoding
// the string's contents, and escaping as appropriate
uint32_t TJSONContext::writeBase64(const std::string& str) {
  uint32_t result = writeNext();
  result += 2; // For quotes
  trans_->write(&kJSONStringDelimiter, 1);
  uint8_t b[4];
  const uint8_t* bytes = (const uint8_t*)str.c_str();
  if (str.length() > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  uint32_t len = static_cast<uint32_t>(str.length());
  while (len >= 3) {
    // Encode 3 bytes at a time
    base64_encode(bytes, 3, b);
    trans_->write(b, 4);
    result += 4;
    bytes += 3;
    len -= 3;
  }
  if (len) { // Handle remainder
    base64_encode(bytes, len, b);
    trans_->write(b, len + 1);
    result += len + 1;
  }
  trans_->write(&kJSONStringDelimiter, 1);
  return result;
}

// Convert the given integer type to a JSON number, or a string
// if the context requires it (eg: key in a map pair).
template <typename NumberType>
uint32_t TJSONContext::writeInteger(NumberType num) {
  uint32_t result = writeNext();
  std::string val(boost::lexical_cast<std::string>(num));
  bool esc = escapeNum();
  if (esc) {
    trans_->write(&kJSONStringDelimiter, 1);
    result += 1;
  }
  if (val.length() > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  trans_->write((const uint8_t*)val.c_str(), static_cast<uint32_t>(val.length()));
  result += static_cast<uint32_t>(val.length());
  if (esc) {
    trans_->write(&kJSONStringDelimiter, 1);
    result += 1;
  }
  return result;
}

template uint32_t TJSONContext::writeInteger<bool>(bool num);
template uint32_t TJSONContext::writeInteger<int8_t>(int8_t num);
template uint32_t TJSONContext::writeInteger<int16_t>(int16_t num);
template uint32_t TJSONContext::writeInteger<int32_t>(int32_t num);
template uint32_t TJSONContext::writeInteger<uint32_t>(uint32_t num);
template uint32_t TJSONContext::writeInteger<int64_t>(int64_t num);
template uint32_t TJSONContext::writeInteger<uint64_t>(uint64_t num);
template uint32_t TJSONContext::writeInteger<TMessageType>(TMessageType num);

namespace {
std::string doubleToString(double d) {
  std::ostringstream str;
  str.imbue(std::locale::classic());
  const double max_digits10 = 2 + std::numeric_limits<double>::digits10;
  str.precision(max_digits10);
  str << d;
  return str.str();
}
}

// Convert the given double to a JSON string, which is either the number,
// "NaN" or "Infinity" or "-Infinity".
uint32_t TJSONContext::writeDouble(double num) {
  uint32_t result = writeNext();
  std::string val;

  bool special = false;
  switch (boost::math::fpclassify(num)) {
  case FP_INFINITE:
    if (boost::math::signbit(num)) {
      val = kThriftNegativeInfinity;
    } else {
      val = kThriftInfinity;
    }
    special = true;
    break;
  case FP_NAN:
    val = kThriftNan;
    special = true;
    break;
  default:
    val = doubleToString(num);
    break;
  }

  bool esc = special || escapeNum();
  if (esc) {
    trans_->write(&kJSONStringDelimiter, 1);
    result += 1;
  }
  if (val.length() > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  trans_->write((const uint8_t*)val.c_str(), static_cast<uint32_t>(val.length()));
  result += static_cast<uint32_t>(val.length());
  if (esc) {
    trans_->write(&kJSONStringDelimiter, 1);
    result += 1;
  }
  return result;
}

// Decodes the four hex parts of a JSON escaped string character and returns
// the UTF-16 code unit via out.
uint32_t TJSONContext::readEscapeChar(uint16_t* out) {
  uint8_t b[4];
  b[0] = reader_.read();
  b[1] = reader_.read();
  b[2] = reader_.read();
  b[3] = reader_.read();

  *out = (hexVal(b[0]) << 12)
    + (hexVal(b[1]) << 8) + (hexVal(b[2]) << 4) + hexVal(b[3]);

  return 4;
}

uint32_t TJSONContext::readTypeID(TType& typeID) {
  std::string tmpString;
  uint32_t result = readString(tmpString);
  typeID = getTypeIDForTypeName(tmpString);
  return result;
}

// Decodes a JSON string, including unescaping, and returns the string via str
uint32_t TJSONContext::readString(std::string& str, bool skipContext) {
  uint32_t result = (skipContext ? 0 : readNext());
  result += readSyntaxChar(reader_, kJSONStringDelimiter);
  std::vector<uint16_t> codeunits;
  uint8_t ch;
  str.clear();
  while (true) {
    ch = reader_.read();
    ++result;
    if (ch == kJSONStringDelimiter) {
      break;
    }
    if (ch == kJSONBackslash) {
      ch = reader_.read();
      ++result;
      if (ch == kJSONEscapeChar) {
        uint16_t cp;
        result += readEscapeChar(&cp);
        if (isHighSurrogate(cp)) {
          codeunits.push_back(cp);
        } else {
          if (isLowSurrogate(cp)
               && codeunits.empty()) {
            throw TProtocolException(TProtocolException::INVALID_DATA,
                                     "Missing UTF-16 high surrogate pair.");
          }
          codeunits.push_back(cp);
          codeunits.push_back(0);
          str += boost::locale::conv::utf_to_utf<char>(codeunits.data());
          codeunits.clear();
        }
        continue;
      } else {
        size_t pos = kEscapeChars.find(ch);
        if (pos == std::string::npos) {
          throw TProtocolException(TProtocolException::INVALID_DATA,
                                   "Expected control char, got '" + std::string((const char*)&ch, 1)
                                   + "'.");
        }
        ch = kEscapeCharVals[pos];
      }
    }
    if (!codeunits.empty()) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                               "Missing UTF-16 low surrogate pair.");
    }
    str += ch;
  }

  if (!codeunits.empty()) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "Missing UTF-16 low surrogate pair.");
  }
  return result;
}

// Reads a block of base64 characters, decoding it, and returns via str
uint32_t TJSONContext::readBase64(std::string& str) {
  std::string tmp;
  uint32_t result = readString(tmp);
  uint8_t* b = (uint8_t*)tmp.c_str();
  if (tmp.length() > (std::numeric_limits<uint32_t>::max)())
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  uint32_t len = static_cast<uint32_t>(tmp.length());
  str.clear();
  // Ignore padding
  if (len >= 2)  {
    uint32_t bound = len - 2;
    for (uint32_t i = len - 1; i >= bound && b[i] == '='; --i) {
      --len;
    }
  }
  while (len >= 4) {
    base64_decode(b, 4);
    str.append((const char*)b, 3);
    b += 4;
    len -= 4;
  }
  // Don't decode if we hit the end or got a single leftover byte (invalid
  // base64 but legal for skip of regular string type)
  if (len > 1) {
    base64_decode(b, len);
    str.append((const char*)b, len - 1);
  }
  return result;
}

// Reads a sequence of characters, stopping at the first one that is not
// a valid JSON numeric character.
uint32_t TJSONContext::readNumericChars(std::string& str) {
  uint32_t result = 0;
  str.clear();
  while (true) {
    uint8_t ch = reader_.peek();
    if (!isJSONNumeric(ch)) {
      break;
    }
    reader_.read();
    str += ch;
    ++result;
  }
  return result;
}

// Reads a sequence of characters and assembles them into a number,
// returning them via num
template <typename NumberType>
uint32_t TJSONContext::readInteger(NumberType& num) {
  uint32_t result = readNext();
  bool esc = escapeNum();
  if (esc) {
    result += readSyntaxChar(reader_, kJSONStringDelimiter);
  }
  std::string str;
  result += readNumericChars(str);
  try {
    num = boost::lexical_cast<NumberType>(str);
  } catch (boost::bad_lexical_cast e) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                                 "Expected numeric value; got \"" + str + "\"");
  }
  if (esc) {
    result += readSyntaxChar(reader_, kJSONStringDelimiter);
  }
  return result;
}

template uint32_t TJSONContext::readInteger<bool>(bool& num);
template uint32_t TJSONContext::readInteger<int8_t>(int8_t& num);
template uint32_t TJSONContext::readInteger<int16_t>(int16_t& num);
template uint32_t TJSONContext::readInteger<int32_t>(int32_t& num);
template uint32_t TJSONContext::readInteger<int64_t>(int64_t& num);
template uint32_t TJSONContext::readInteger<uint64_t>(uint64_t& num);

namespace {
double stringToDouble(const std::string& s) {
  double d;
  std::istringstream str(s);
  str.imbue(std::locale::classic());
  str >> d;
  if (str.bad() || !str.eof())
    throw std::runtime_error(s);
  return d;
}
}

// Reads a JSON number or string and interprets it as a double.
uint32_t TJSONContext::readDouble(double& num) {
  uint32_t result = readNext();
  std::string str;
  if (reader_.peek() == kJSONStringDelimiter) {
    result += readString(str, true);
    // Check for NaN, Infinity and -Infinity
    if (str == kThriftNan) {
      num = HUGE_VAL / HUGE_VAL; // generates NaN
    } else if (str == kThriftInfinity) {
      num = HUGE_VAL;
    } else if (str == kThriftNegativeInfinity) {
      num = -HUGE_VAL;
    } else {
      if (!escapeNum()) {
        // Throw exception -- we should not be in a string in this case
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                     "Numeric data unexpectedly quoted");
      }
      try {
        num = stringToDouble(str);
      } catch (std::runtime_error e) {
        throw TProtocolException(TProtocolException::INVALID_DATA,
                                     "Expected numeric value; got \"" + str + "\"");
      }
    }
  } else {
    if (escapeNum()) {
      // This will throw - we should have had a quote if escapeNum == true
      readSyntaxChar(reader_, kJSONStringDelimiter);
    }
    result += readNumericChars(str);
    try {
      num = stringToDouble(str);
    } catch (std::runtime_error e) {
      throw TProtocolException(TProtocolException::INVALID_DATA,
                                   "Expected numeric value; got \"" + str + "\"");
    }
  }
  return result;
}

uint32_t TJSONContext::readObject(transport::TMemoryBuffer& buf) {
  uint32_t result = readNext();
  result += readSyntaxChar(reader_, kJSONObjectStart);
  buf.write(&kJSONObjectStart, 1);
  uint32_t nesting = 1;
  while (nesting) {
    uint8_t ch = reader_.read();
    buf.write(&ch, 1);
    result++;
    // FIXME escape {} in strings
    if (ch == kJSONObjectStart) {
      nesting++;
    }
    if (ch == kJSONObjectEnd) {
      nesting--;
    }
  }
  return result;
}

uint8_t TJSONContext::peek() {
  return reader_.peek();
}

// JSONPairContext

TJSONPairContext::TJSONPairContext(boost::shared_ptr<TTransport> trans)
  : TJSONContext(trans),
    first_(true),
    colon_(true) {
}

uint32_t TJSONPairContext::writeNext() {
  if (first_) {
    first_ = false;
    colon_ = true;
    return 0;
  } else {
    transport()->write(colon_ ? &kJSONPairSeparator : &kJSONElemSeparator, 1);
    colon_ = !colon_;
    return 1;
  }
}

uint32_t TJSONPairContext::readNext() {
  if (first_) {
    first_ = false;
    colon_ = true;
    return 0;
  } else {
    uint8_t ch = (colon_ ? kJSONPairSeparator : kJSONElemSeparator);
    colon_ = !colon_;
    return readSyntaxChar(reader(), ch);
  }
}

bool TJSONPairContext::escapeNum() {
  return colon_;
}

uint32_t TJSONPairContext::writeStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->writeNext();
  transport()->write(&kJSONObjectStart, 1);
  return result + 1;
}

uint32_t TJSONPairContext::writeEnd() {
  transport()->write(&kJSONObjectEnd, 1);
  return 1;
}

uint32_t TJSONPairContext::readStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->readNext();
  result += readSyntaxChar(reader(), kJSONObjectStart);
  return result;
}

uint32_t TJSONPairContext::readEnd() {
  return readSyntaxChar(reader(), kJSONObjectEnd);
}

// JSONListContext

TJSONListContext::TJSONListContext(boost::shared_ptr<TTransport> trans)
  : TJSONContext(trans),
    first_(true) {
}

uint32_t TJSONListContext::writeNext() {
  if (first_) {
    first_ = false;
    return 0;
  } else {
    transport()->write(&kJSONElemSeparator, 1);
    return 1;
  }
}

uint32_t TJSONListContext::readNext() {
  if (first_) {
    first_ = false;
    return 0;
  } else {
    return readSyntaxChar(reader(), kJSONElemSeparator);
  }
}

uint32_t TJSONListContext::writeStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->writeNext();
  transport()->write(&kJSONArrayStart, 1);
  return result + 1;
}

uint32_t TJSONListContext::writeEnd() {
  transport()->write(&kJSONArrayEnd, 1);
  return 1;
}

uint32_t TJSONListContext::readStart(boost::shared_ptr<TJSONContext> parent) {
  uint32_t result = parent->readNext();
  result += readSyntaxChar(reader(), kJSONArrayStart);
  return result;
}

uint32_t TJSONListContext::readEnd() {
  return readSyntaxChar(reader(), kJSONArrayEnd);
}

// Context stack

TJSONContextStack::TJSONContextStack(boost::shared_ptr<TTransport> trans)
  : context_(new TJSONContext(trans)) {
}

boost::shared_ptr<TJSONContext> TJSONContextStack::top() {
  return context_;
}

uint32_t TJSONContextStack::pushRead(boost::shared_ptr<TJSONContext> c) {
  uint32_t result = c->readStart(context_);
  push(c);
  return result;
}

uint32_t TJSONContextStack::pushWrite(boost::shared_ptr<TJSONContext> c) {
  uint32_t result = c->writeStart(context_);
  push(c);
  return result;
}

void TJSONContextStack::push(boost::shared_ptr<TJSONContext> c) {
  contexts_.push(context_);
  context_ = c;
}

uint32_t TJSONContextStack::popRead() {
  uint32_t result = context_->readEnd();
  pop();
  return result;
}

uint32_t TJSONContextStack::popWrite() {
  uint32_t result = context_->writeEnd();
  pop();
  return result;
}

void TJSONContextStack::pop() {
  context_ = contexts_.top();
  contexts_.pop();
}

}
}
} // apache::thrift::protocol
