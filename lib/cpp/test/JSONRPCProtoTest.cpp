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

#define _USE_MATH_DEFINES
#include <thrift/TApplicationException.h>
#include <thrift/protocol/TJSONProtocol.h>
#include <thrift/protocol/TJSONRPCProtocol.h>
#include "gen-cpp/Srv.h"

#define BOOST_TEST_MODULE JSONRPCProtoTest
#include <boost/test/unit_test.hpp>

using namespace thrift::test::debug;
using apache::thrift::transport::TMemoryBuffer;
using apache::thrift::protocol::TProtocol;
using apache::thrift::protocol::TJSONRPCProtocol;
using apache::thrift::protocol::TJSONProtocol;

BOOST_AUTO_TEST_CASE(test_jsonrpc_message_write) {
  {
    const std::string expected_result(
      "{\"jsonrpc\":\"2.0\",\"method\":\"primitiveMethod\",\"params\":{},\"id\":0}"
    );

    boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> proto(new TJSONRPCProtocol(buffer));
    SrvClient client(proto);
    client.send_primitiveMethod();

    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    const std::string result((char*)buf, (unsigned int)size);
    BOOST_CHECK_MESSAGE(!expected_result.compare(result),
      "Expected:\n" << expected_result << "\nGotten:\n" << result);
  }
  {
    const std::string expected_result(
      "{\"jsonrpc\":\"2.0\",\"result\":{\"0\":{\"i32\":21}},\"id\":999}"
    );

    boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> proto(new TJSONRPCProtocol(buffer));

    Srv_primitiveMethod_result ret;
    ret.success = 21;
    ret.__isset.success = 1;
    proto->writeMessageBegin("primitiveMethod", ::apache::thrift::protocol::T_REPLY, 999);
    ret.write(proto.get());
    proto->writeMessageEnd();

    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    const std::string result((char*)buf, (unsigned int)size);

    BOOST_CHECK_MESSAGE(!expected_result.compare(result),
      "Expected:\n" << expected_result << "\nGotten:\n" << result);
  }
  {
    const std::string expected_result(
      "{\"jsonrpc\":\"2.0\",\"method\":\"onewayMethod\",\"params\":{}}"
    );

    boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
    boost::shared_ptr<TJSONRPCProtocol> proto(new TJSONRPCProtocol(buffer));

    SrvClient client(proto);
    client.send_onewayMethod();

    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    const std::string result((char*)buf, (unsigned int)size);

    BOOST_CHECK_MESSAGE(!expected_result.compare(result),
      "Expected:\n" << expected_result << "\nGotten:\n" << result);
  }
  {
    const std::string expected_result(
      "{\"jsonrpc\":\"2.0\",\"method\":\"methodWithDefaultArgs\",\"params\":{\"1\":{\"i32\":55}},\"id\":0}"
    );

    boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
    boost::shared_ptr<TJSONRPCProtocol> proto(new TJSONRPCProtocol(buffer));

    SrvClient client(proto);
    client.send_methodWithDefaultArgs(55);

    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    const std::string result((char*)buf, (unsigned int)size);

    BOOST_CHECK_MESSAGE(!expected_result.compare(result),
      "Expected:\n" << expected_result << "\nGotten:\n" << result);
  }
  {
    const std::string expected_result(
      "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"Thrift exception\",\"data\":{\"1\":{\"str\":\"Exception\"},\"2\":{\"i32\":0}}},\"id\":999}"
    );

    boost::shared_ptr<TMemoryBuffer> buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> proto(new TJSONRPCProtocol(buffer));

    ::apache::thrift::TApplicationException x("Exception");
    proto->writeMessageBegin("voidMethod", ::apache::thrift::protocol::T_EXCEPTION, 999);
    x.write(proto.get());
    proto->writeMessageEnd();

    uint8_t* buf;
    uint32_t size;
    buffer->getBuffer(&buf, &size);
    const std::string result((char*)buf, (unsigned int)size);

    BOOST_CHECK_MESSAGE(!expected_result.compare(result),
      "Expected:\n" << expected_result << "\nGotten:\n" << result);
  }
}

BOOST_AUTO_TEST_CASE(test_jsonrpc_message_read) {
  {
    // Check method call.
    const uint8_t request[] =
      "{\"jsonrpc\":\"2.0\",\"method\":\"Janky\",\"params\":{\"1\":{\"i32\":100}},\"id\":1}";
    const std::string expected_response(
      "{\"jsonrpc\":\"2.0\",\"result\":{\"0\":{\"i32\":0}},\"id\":1}"
    );

    boost::shared_ptr<TMemoryBuffer> in_buffer(new TMemoryBuffer());
    in_buffer->write(request, sizeof(request));
    boost::shared_ptr<TProtocol> in_proto(new TJSONRPCProtocol(in_buffer));
    boost::shared_ptr<TMemoryBuffer> out_buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> out_proto(new TJSONRPCProtocol(out_buffer));
    SrvProcessor server(boost::shared_ptr<SrvNull>(new SrvNull()));
    server.process(in_proto, out_proto, NULL);

    const std::string result(out_buffer->getBufferAsString());
    BOOST_CHECK_MESSAGE(!expected_response.compare(result),
      "Expected:\n" << expected_response << "\nGotten:\n" << result);
  }
  {
    // Check void method call.
    const uint8_t request[] =
      "{\"jsonrpc\":\"2.0\",\"method\":\"voidMethod\",\"id\":2}";
    const std::string expected_response(
      "{\"jsonrpc\":\"2.0\",\"result\":{},\"id\":2}"
    );

    boost::shared_ptr<TMemoryBuffer> in_buffer(new TMemoryBuffer());
    in_buffer->write(request, sizeof(request));
    boost::shared_ptr<TProtocol> in_proto(new TJSONRPCProtocol(in_buffer));
    boost::shared_ptr<TMemoryBuffer> out_buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> out_proto(new TJSONRPCProtocol(out_buffer));
    SrvProcessor server(boost::shared_ptr<SrvNull>(new SrvNull()));
    server.process(in_proto, out_proto, NULL);

    const std::string result(out_buffer->getBufferAsString());
    BOOST_CHECK_MESSAGE(!expected_response.compare(result),
      "Expected:\n" << expected_response << "\nGotten:\n" << result);
  }
  {
    // Check method call with empty (non-existent) params.
    const uint8_t request[] =
      "{\"jsonrpc\":\"2.0\",\"method\":\"primitiveMethod\",\"id\":55}";
    const std::string expected_response(
      "{\"jsonrpc\":\"2.0\",\"result\":{\"0\":{\"i32\":0}},\"id\":55}"
    );

    boost::shared_ptr<TMemoryBuffer> in_buffer(new TMemoryBuffer());
    in_buffer->write(request, sizeof(request));
    boost::shared_ptr<TProtocol> in_proto(new TJSONRPCProtocol(in_buffer));
    boost::shared_ptr<TMemoryBuffer> out_buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> out_proto(new TJSONRPCProtocol(out_buffer));
    SrvProcessor server(boost::shared_ptr<SrvNull>(new SrvNull()));
    server.process(in_proto, out_proto, NULL);

    const std::string result(out_buffer->getBufferAsString());
    BOOST_CHECK_MESSAGE(!expected_response.compare(result),
      "Expected:\n" << expected_response << "\nGotten:\n" << result);
  }
  {
    // Check oneway method call.
    const uint8_t request[] =
      "{\"jsonrpc\":\"2.0\",\"method\":\"onewayMethod\"}";
    const std::string expected_response;

    boost::shared_ptr<TMemoryBuffer> in_buffer(new TMemoryBuffer());
    in_buffer->write(request, sizeof(request));
    boost::shared_ptr<TProtocol> in_proto(new TJSONRPCProtocol(in_buffer));
    boost::shared_ptr<TMemoryBuffer> out_buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> out_proto(new TJSONRPCProtocol(out_buffer));
    SrvProcessor server(boost::shared_ptr<SrvNull>(new SrvNull()));
    server.process(in_proto, out_proto, NULL);

    const std::string result(out_buffer->getBufferAsString());
    BOOST_CHECK_MESSAGE(!expected_response.compare(result),
      "Expected:\n" << expected_response << "\nGotten:\n" << result);
  }
  {
    // Check unknown method exception.
    const uint8_t request[] =
      "{\"jsonrpc\":\"2.0\",\"method\":\"invalidMethod\",\"params\":{},\"id\":99}";
    const std::string expected_response(
      "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"Thrift exception\",\"data\":{\"1\":{\"str\":\"Invalid method name: 'invalidMethod'\"},\"2\":{\"i32\":1}}},\"id\":99}"
    );

    boost::shared_ptr<TMemoryBuffer> in_buffer(new TMemoryBuffer());
    in_buffer->write(request, sizeof(request));
    boost::shared_ptr<TProtocol> in_proto(new TJSONRPCProtocol(in_buffer));
    boost::shared_ptr<TMemoryBuffer> out_buffer(new TMemoryBuffer());
    boost::shared_ptr<TProtocol> out_proto(new TJSONRPCProtocol(out_buffer));
    SrvProcessor server(boost::shared_ptr<SrvNull>(new SrvNull()));
    server.process(in_proto, out_proto, NULL);

    const std::string result(out_buffer->getBufferAsString());
    BOOST_CHECK_MESSAGE(!expected_response.compare(result),
      "Expected:\n" << expected_response << "\nGotten:\n" << result);
  }
}
