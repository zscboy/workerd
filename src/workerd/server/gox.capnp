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
}

struct DestroyWorkerdInput {
  id       @0:Text;
}
