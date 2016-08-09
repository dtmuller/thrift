#ifndef _THRIFT_PROTOCOL_TJSONUTILS_H_
#define _THRIFT_PROTOCOL_TJSONUTILS_H_ 1

#include <sstream>
#include <stack>

#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TBufferTransports.h>

using namespace apache::thrift::transport;

namespace apache {
namespace thrift {
namespace protocol {

class TJSONPairContext;
class TJSONListContext;

 // Static data

 static const uint8_t kJSONObjectStart = '{';
 static const uint8_t kJSONObjectEnd = '}';
 static const uint8_t kJSONArrayStart = '[';
 static const uint8_t kJSONArrayEnd = ']';
 static const uint8_t kJSONPairSeparator = ':';
 static const uint8_t kJSONElemSeparator = ',';
 static const uint8_t kJSONBackslash = '\\';
 static const uint8_t kJSONStringDelimiter = '"';
 static const uint8_t kJSONZeroChar = '0';
 static const uint8_t kJSONEscapeChar = 'u';

/**
 * Class to serve as base JSON context and as base class for other context
 * implementations
 */
class TJSONContext {
  friend class TJSONPairContext;
  friend class TJSONListContext;

public:
  TJSONContext(boost::shared_ptr<TTransport> trans);
  virtual ~TJSONContext();

protected:
  /**
   * Write context data to the transport. Default is to do nothing.
   */
  virtual uint32_t writeNext();
  /**
   * Read context data from the transport. Default is to do nothing.
   */
  virtual uint32_t readNext();

  /**
   * Return true if numbers need to be escaped as strings in this context.
   * Default behavior is to return false.
   */
  virtual bool escapeNum();

public:
  /**
   * Write opening sequence to the transport. Default is to do nothing.
   */
  virtual uint32_t writeStart(boost::shared_ptr<TJSONContext> parent);
  /**
   * Write closing sequence to the transport. Default is to do nothing.
   */
  virtual uint32_t writeEnd();
  /**
   * Read opening sequence from the transport. Default is to do nothing.
   */
  virtual uint32_t readStart(boost::shared_ptr<TJSONContext> parent);
  /**
   * Read closing sequence from the transport. Default is to do nothing.
   */
  virtual uint32_t readEnd();

  uint32_t writeEscapeChar(uint8_t ch);
  uint32_t writeChar(uint8_t ch);
  uint32_t writeTypeID(TType typeID);
  uint32_t writeString(const std::string& str);
  uint32_t writeBase64(const std::string& str);

  template <typename NumberType>
  uint32_t writeInteger(NumberType num);

  uint32_t writeDouble(double num);
  uint32_t readEscapeChar(uint16_t* out);
  uint32_t readTypeID(TType& typeID);
  uint32_t readString(std::string& str, bool skipContext = false);
  uint32_t readBase64(std::string& str);
  uint32_t readNumericChars(std::string& str);

  template <typename NumberType>
  uint32_t readInteger(NumberType& num);

  uint32_t readDouble(double& num);
  uint32_t readObject(transport::TMemoryBuffer& buf);

  class LookaheadReader {

  public:
    LookaheadReader(boost::shared_ptr<TTransport> trans) : trans_(trans), hasData_(false) {}

    uint8_t read() {
      if (hasData_) {
        hasData_ = false;
      } else {
        trans_->readAll(&data_, 1);
      }
      return data_;
    }

    uint8_t peek() {
      if (!hasData_) {
        trans_->readAll(&data_, 1);
      }
      hasData_ = true;
      return data_;
    }

  private:
    boost::shared_ptr<TTransport> trans_;
    bool hasData_;
    uint8_t data_;
  };

  uint8_t peek();

public:
  boost::shared_ptr<TTransport> transport();
  LookaheadReader& reader();

private:
  boost::shared_ptr<TTransport> trans_;
  LookaheadReader reader_;
};

// Context class for object member key-value pairs
class TJSONPairContext : public TJSONContext {
public:
  TJSONPairContext(boost::shared_ptr<TTransport> trans);

protected:
  uint32_t writeNext();
  uint32_t readNext();

  // Numbers must be turned into strings if they are the key part of a pair
  virtual bool escapeNum();

public:
  virtual uint32_t writeStart(boost::shared_ptr<TJSONContext> parent);
  virtual uint32_t writeEnd();
  virtual uint32_t readStart(boost::shared_ptr<TJSONContext> parent);
  virtual uint32_t readEnd();

private:
  bool first_;
  bool colon_;
};

// Context class for lists
class TJSONListContext : public TJSONContext {
public:
  TJSONListContext(boost::shared_ptr<TTransport> trans);

protected:
  uint32_t writeNext();
  uint32_t readNext();

public:
  virtual uint32_t writeStart(boost::shared_ptr<TJSONContext> parent);
  virtual uint32_t writeEnd();
  virtual uint32_t readStart(boost::shared_ptr<TJSONContext> parent);
  virtual uint32_t readEnd();

private:
  bool first_;
};

// Context stack
class TJSONContextStack {
public:
  TJSONContextStack(boost::shared_ptr<TTransport> trans);

public:
  boost::shared_ptr<TJSONContext> top();

  uint32_t pushRead(boost::shared_ptr<TJSONContext> c);
  uint32_t pushWrite(boost::shared_ptr<TJSONContext> c);
  uint32_t popRead();
  uint32_t popWrite();

private:
  void push(boost::shared_ptr<TJSONContext> c);
  void pop();

private:
  std::stack<boost::shared_ptr<TJSONContext> > contexts_;
  boost::shared_ptr<TJSONContext> context_;
};

}
}
} // apache::thrift::protocol

#endif // _THRIFT_PROTOCOL_TJSONUTILS_H_
