@0x9ff0cba9afaed107;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("workerd::server::gox");
$Cxx.allowCancellation;

struct JSONCallInput {
  method @0:Text;
  args   @1:Text;
}

struct JSONCallOutput {
  code @0:Int32;
  msg  @1:Text;
}

struct InitRuntimeInput {
  reportURL  @0:Text;
}

struct CreateWorkerdInput {
  id       @0:Text;
  directory  @1:Text;
  configFile @2:Text;
  socketAddr @3:Text;
}

struct DestroyWorkerdInput {
  id       @0:Text;
}

struct QueryWorkerdInput {
  id       @0:Text;
}

struct QueryWorkerdOutput {
  code   @0:Int32;
  error  @1:Text;
  state  @2:Int32;
}
