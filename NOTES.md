# General

## Thrift IDL

```
struct GeneratedServerStruct
{
	1000: i32 x,
	5000: optional i32 y,
}

exception GeneratedServerException
{
	1: i32 err,
	2: string str
}

service OneOfEachService
{
	oneway void OnewayMethod(1: i32 arg)
	bool BooleanMethod(1: bool arg1, 2: bool arg2)
	i32 IntegerMethod(1: i32 arg1, 2: i32 arg2)
	//double DoubleMethod(1: double arg1, 2: double arg2)
	string StringMethod(1: string arg1, 2: string arg2)
	list<i32> ListMethod(1: list<i32> arg1)
	set<i32> SetMethod(1: set<i32> arg1)
	map<i32,string> MapMethod(1: map<i32,string> arg1)
	GeneratedServerStruct StructMethod(1: GeneratedServerStruct arg1)
	i32 ExceptionMethod(1: i32 err, 2: string str) throws (1: GeneratedServerException exp1)
}
```

## OnewayMethod

__Thrift__

```
service OneOfEachService
{
	oneway void OnewayMethod(1: i32 arg)
}
```

Thrift oneway methods correspond to notifications in JSONRPC. Notifications
missing the id field.

__JSON Protocol__

```json
[1,"OnewayMethod",4,0,{"1":{"i32":99}}]
```

__JSONRPC Protocol__

```json
{
    "jsonrpc": "2.0",
    "method": "OnewayMethod",
    "params": {"1":{"i32":99}},
}
```

## IntegerMethod

__Thrift__

```
service OneOfEachService
{
	i32 IntegerMethod(1: i32 arg1, 2: i32 arg2)
}
```

__JSON Protocol__

```json
[1,"IntegerMethod",1,2,{"1":{"i32":55},"2":{"i32":99}}]
[1,"IntegerMethod",2,2,{"0":{"i32":55}}]
```

__JSONRPC Protocol__

```json
{
    "jsonrpc": "2.0",
    "method": "IntegerMethod",
    "params": {"1":{"i32":55},"2":{"i32":99}},
    "id": 2
}
{
    "jsonrpc": "2.0",
    "result": {"0":{"i32":55}},
    "id": 2
}
```

## IntegerMethod (called with invalid argument)

__JSON Protocol__

```json
[1,"IntegerMethod",1,2,{"1":{"i16":55},"2":{"i32":99}}]
[1,"IntegerMethod",3,2,{"1":{"str":"TProtocolException: Invalid data"},"2":{"i32":7}}]
```

__JSONRPC Protocol__

```json
{
    "jsonrpc": "2.0",
    "method": "IntegerMethod",
    "params": {"1":{"i16":55},"2":{"i32":99}},
    "id": 2
}
{
    "jsonrpc": "2.0",
    "error":{"code": ???,"message": ???},
    "data": {"1":{"str":"TProtocolException: Invalid data"},"2":{"i32":7}},
    "id": 2
}
```

## ListMethod

__Thrift__

```
service OneOfEachService
{
	list<i32> ListMethod(1: list<i32> arg1)
}
```

__JSON Protocol__

```json
[1,"ListMethod",1,4,{"1":{"lst":["i32",2,55,99]}}]
[1,"ListMethod",2,4,{"0":{"lst":["i32",2,55,99]}}]
```

__JSONRPC Protocol__

```json
{
    "jsonrpc": "2.0",
    "method": "ListMethod",
    "params": {"1":{"lst":["i32",2,55,99]}},
    "id": 4
}
{
    "jsonrpc": "2.0",
    "result": {"0":{"lst":["i32",2,55,99]}},
    "id": 4
}
```

## ExceptionMethod

__Thrift__

```
exception GeneratedServerException
{
	1: i32 err,
	2: string str
}

service OneOfEachService
{
	i32 ExceptionMethod(1: i32 err, 2: string str) throws (1: GeneratedServerException exp1)
}
```
__JSON Protocol__

```json
[1,"ExceptionMethod",1,8,{"1":{"i32":-999},"2":{"str":"errstr"}}]
[1,"ExceptionMethod",2,8,{"1":{"i32":-999},"2":{"str":"errstr"}}]
```

__JSONRPC Protocol__

```json
{
    "jsonrpc": "2.0",
    "method": "ExceptionMethod",
    "params": {"1":{"i32":-999},"2":{"str":"errstr"}},
    "id": 8
}
{
    "jsonrpc": "2.0",
    "result": {"1":{"i32":-999},"2":{"str":"errstr"}},
    "id": 8
}
```

# Reader

## THRIFT reading mechanism

The THRIFT reading mechanism implies ordered JSON data (JSON array). However the
JSONRPC protocol uses an (unordered) JSON object instead.

Therefore the standard THRIFT reading mechanism (within generated code) won't
work anymore:

```cpp
readMessageBegin(std::string& name, TMessageType& messageType, int32_t& seqid);
// read whatever data is expected
// ...
readMessageEnd();

// Works with {"jsonrpc":"2.0",method="myMethod",id=2,params:{}}
// but not with {"jsonrpc":"2.0",method="myMethod",params:{},id=2}
```
