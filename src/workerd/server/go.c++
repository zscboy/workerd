#include "go.h"
#include <capnp/compat/json.h>
#include <string.h>
#include <kj/timer.h>
#include "server.h"
#include <kj/thread.h>

#if _WIN32
#include <kj/async-win32.h>
#else
#include <unistd.h>
#include <kj/async-unix.h>
#endif

// for std::cin
#include <iostream>

static const char* heepStrPtr(const char* ptr, int maxlen=256) {
  auto length = strnlen(ptr, maxlen);
  auto heapPtr = new char[length+1];
  std::memcpy(heapPtr, ptr, length);
  heapPtr[length] = 0; // null-terminate

  return heapPtr;
}

void workerdGoFreeHeapStrPtr(const char* ptr) {
  delete [] ptr;
}

const char* workerdGoRuntimeJsonCall(const char* jsonString) {
  auto& runtime =  workerd::server::WorkerdGoRuntime::instance();
  if (jsonString == nullptr) {
    return heepStrPtr("{\"code\":-1,\"msg\":\"null json object\"}");
  }

  kj::Maybe<kj::String> result = kj::none;
  kj::Maybe<kj::Exception> maybeException = kj::runCatchingExceptions([&]() {
    result = runtime.onJsonCall(jsonString);
  });

  KJ_IF_SOME(e, maybeException) {
    // handle exception
    result = kj::str("{\"code\":-1,\"msg\":\"", e.getDescription(), "\"}");
  }

  KJ_IF_SOME(callResult, result) {
    return heepStrPtr(callResult.cStr(), callResult.size());
  }

  return heepStrPtr("{\"code\":-1,\"msg\":\"empty call result\"}");
}

namespace workerd::server {

kj::Maybe<kj::Own<capnp::SchemaFile>> tryImportBulitin(kj::StringPtr name) {
  if (name == "/capnp/c++.capnp") {
    return kj::heap<BuiltinSchemaFileImpl>("/capnp/c++.capnp", CPP_CAPNP_SCHEMA);
  } else if (name == "/workerd/workerd.capnp") {
    return kj::heap<BuiltinSchemaFileImpl>("/workerd/workerd.capnp", WORKERD_CAPNP_SCHEMA);
  } else {
    return kj::none;
  }
}

int GoWorkerd::init(void) {
  int socketCnt = 0;
  kj::StringPtr socketName;
  kj::Maybe<kj::Exception> maybeException = kj::runCatchingExceptions([&]() {
    auto path = kj::Path(nullptr);
    path = path.evalNative(mDir);
    path = path.append(mConfigFile);
    #if _WIN32
        auto newPath = path.slice(1, path.size());
        path = newPath.clone();
    #endif
    parseConfigFile(path.toString());

    // ensure we have at least one top-level config object
    auto config = getConfig();
    for (auto sock: config.getSockets()) {
      socketCnt++;
      socketName = sock.getName();
    }
  });

  KJ_IF_SOME(e, maybeException) {
    // handle exception
    mError = kj::str("GoWorkerd::init exception: ", e.getDescription());
    return -1;
  }

  // maybe config has error, 'mError' set by 'reportParsingError'
  if (mError.size() > 0) {
    // error occurs
    return -1;
  }

  if (socketCnt > 1) {
    mError = kj::str("GoWorkerd::init config file has more than 1 sockets: ", socketCnt);
    return -1;
  }

  if (socketCnt > 0 && (socketName == nullptr || socketName.size() == 0)) {
    mError = kj::str("GoWorkerd::init config file socket has no name");
    return -1;
  }

  auto thread = kj::Thread([&]() {
    serveImpl();
  });

  // don't join thread handle when thread object destruct
  thread.detach();

  // when serve-thread has exception, 'mServeThreadRunning' maybe always false
  auto tryi = 0;
  while (!mServeThreadRunning && tryi < 10) {
    #if _WIN32
        Sleep(100);
    #else
        usleep(100);
    #endif
    tryi++;
  }

  // maybe 'Server' has error, set by 'Server' constructor callback
  if (mError.size() > 0) {
    // error occurs
    return -1;
  }

  return 0;
}

void GoWorkerd::reportParsingError(kj::StringPtr file,
    capnp::SchemaFile::SourcePos start, capnp::SchemaFile::SourcePos end,
    kj::StringPtr message) {
  if (start.line == end.line && start.column < end.column) {
    mError = kj::str(
        file, ":", start.line+1, ":", start.column+1, "-", end.column+1,  ": ", message);
  } else {
    mError = kj::str(
        file, ":", start.line+1, ":", start.column+1, ": ", message);
  }
}

void GoWorkerd::parseConfigFile(kj::StringPtr pathStr) {
  // Read file from disk.
  auto path = mFS->getCurrentPath().evalNative(pathStr);
  auto file = KJ_UNWRAP_OR(mFS->getRoot().tryOpenFile(path), {
    kj::Exception e = KJ_EXCEPTION(FAILED, "file not exist:", path);
    kj::throwFatalException(kj::mv(e));
  });

  // Interpret as schema file.
  mSchemaParser.loadCompiledTypeAndDependencies<config::Config>();

  mParsedSchema = mSchemaParser.parseFile(
      kj::heap<SchemaFileImpl>(mFS->getRoot(), mFS->getCurrentPath(),
          kj::mv(path), nullptr, mImportPath, kj::mv(file), *this));

  // Construct a list of top-level constants of type `Config`. If there is exactly one,
  // we can use it by default.
  for (auto nested: mParsedSchema.getAllNested()) {
    if (nested.getProto().isConst()) {
      auto constSchema = nested.asConst();
      auto type = constSchema.getType();
      if (type.isStruct() &&
          type.asStruct().getProto().getId() == capnp::typeId<config::Config>()) {
        mTopLevelConfigConstants.add(constSchema);
      }
    }
  }
}

config::Config::Reader GoWorkerd::getConfig() {
    KJ_IF_SOME(c, mConfig) {
      return c;
    } else {
      // The optional `<const-name>` parameter must not have been given -- otherwise we would have
      // a non-null `config` by this point. See if we can infer the correct constant...
      if (mTopLevelConfigConstants.empty()) {
        // should not be here
        kj::Exception e = KJ_EXCEPTION(FAILED, "top level config is not exist");
        kj::throwFatalException(kj::mv(e));
      } else {
        return mConfig.emplace(mTopLevelConfigConstants[0].as<config::Config>());
      }
    }
}

void GoWorkerd::serveImpl() {
  KJ_LOG(INFO, kj::str("GoWorkerd serveImpl enter, id: ", mId));

  kj::PromiseFulfillerPair<void> paf = kj::newPromiseAndFulfiller<void>();
  mAbortFulFill = kj::mv(paf.fulfiller);
  auto localAbortPromise = kj::mv(paf.promise);

  auto config = getConfig();

  kj::Maybe<kj::Exception> maybeException = kj::runCatchingExceptions([&]() {
    // for async io and event loop
    kj::AsyncIoContext io = kj::setupAsyncIo();
    NetworkWithLoopback network = { io.provider->getNetwork(), *io.provider };
    EntropySourceImpl entropySource;
    // save executor for later threads communication
    mExecutor = kj::getCurrentThreadExecutor();

    kj::Own<Server> server = kj::heap<Server>(*mFS, io.provider->getTimer(), network, entropySource,
              workerd::Worker::ConsoleMode::STDOUT, [&](kj::String error){
                mError = kj::str(error);

                KJ_LOG(ERROR, kj::str("GoWorkerd server error occurs, id: ", mId, ", error:", mError));
              });

    kj::StringPtr name;
    for (auto sock: config.getSockets()) {
      name = sock.getName();
      break;
    }

    if (name != nullptr && name.size() > 0) {
      server->overrideSocket(kj::str(name), kj::str(mAddr));
    }

    auto func = [&](jsg::V8System& v8System, config::Config::Reader config) {

  #if _WIN32
        return server->run(v8System, config);
  #else
        return server->run(v8System, config,
            // Gracefully drain when SIGTERM is received.
            io.unixEventPort.onSignal(SIGTERM).ignoreResult());
  #endif
    };

    auto promise = func(mV8System, config);
    promise = promise.exclusiveJoin(kj::mv(localAbortPromise));

    mServeThreadRunning = true;
    promise.wait(io.waitScope);

    // destroy server
    server = nullptr;
  });

  KJ_IF_SOME(e, maybeException) {
    // handle exception
    mError = kj::str("GoWorkerd::serveImpl exception: ", e.getDescription());
  }

  mExecutor = kj::none;
  mServeThreadRunning = false;

  KJ_LOG(INFO, kj::str("GoWorkerd serveImpl exit, id: ", mId));
}

int GoWorkerd::destroy() {
  stopServeThread();
  return 0;
}

void GoWorkerd::stopServeThread() {
  if (mServeThreadRunning) {
    KJ_IF_SOME(e, mExecutor) {
      e.executeSync([&](){
        // mAbortFulFill should not be nullptr
        if (mAbortFulFill->isWaiting()) {
          mAbortFulFill->fulfill();
        }
      });

      while (mServeThreadRunning) {
        #if _WIN32
          Sleep(100);
        #else
          usleep(100);
        #endif
      }
    }
  }
}

kj::String WorkerdGoRuntime::onJsonCall(const char* jsonString) {
#if __linux__
  kj::Duration timeout = 5 * kj::SECONDS;
  auto maybelock = mLock.lockExclusiveWithTimeout(timeout);

  if (maybelock == kj::none) {
    return kj::str("{\"code\":-1,\"msg\":\"lock timeout\"}");
  }
#else
  auto maybelock = mLock.lockExclusive();
#endif

  capnp::MallocMessageBuilder inputMessage;
  capnp::JsonCodec json;
  json.handleByAnnotation<gox::JSONCallInput>();
  kj::StringPtr input = jsonString;
  auto msgBuilder = inputMessage.initRoot<gox::JSONCallInput>();
  try {
    json.decode(input, msgBuilder);
  } catch (kj::Exception& ex) {
    return kj::str("{\"code\":-1,\"msg\":\"", ex.getDescription(),"\"}");
  }

  auto method = kj::str(msgBuilder.getMethod());
  auto args = kj::str(msgBuilder.getArgs());

  KJ_LOG(INFO, kj::str("onJsonCall method: ", method, ", with args: ", args));

  kj::String callResult;

  if (method == ("initRuntime")) {
    // init worker-d/golang runtime
    if (mInitialed) {
      callResult = kj::str("{\"code\":-1,\"msg\":\"runtime has been initialized\"}");
    } else {
      callResult = onInitRuntime(args);
    }
  } else {
    // all other methods need 'runtime' has been initialized
    if (!mInitialed) {
      callResult = kj::str("{\"code\":-1,\"msg\":\"runtime not init\"}");
    } else {
      if (method == ("createWorkerd")) {
        // create a nwe worker-d instance
        callResult = onCeateWorkerd(args);
      } else if (method == ("destroyWorkerd")) {
        // destroy worker-d instance and free its resource
        callResult = onDestroyWorkerd(args);
      }  else if (method == ("queryWorkerd")) {
        // query workerd instance state
        callResult = onQueryWorkerd(args);
      } else {
        callResult = kj::str("{\"code\":-1,\"msg\":\"unknown method:", method, "\"}");
      }
    }
  }

  return callResult;
}

kj::String WorkerdGoRuntime::onInitRuntime(const kj::String& args) {
  capnp::MallocMessageBuilder inputMessage;
  capnp::JsonCodec json;
  json.handleByAnnotation<gox::InitRuntimeInput>();
  kj::StringPtr input = args;
  auto msgBuilder = inputMessage.initRoot<gox::InitRuntimeInput>();
  try {
    json.decode(input, msgBuilder);
  } catch (kj::Exception& ex) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", ex.getDescription(),"\"}");
    return kjstr;
  }

  mReportURL = kj::str(msgBuilder.getReportURL());
  if (mReportURL.size() == 0) {
     KJ_LOG(WARNING, "init runtime with empty report url, events will not be report");
  }

  // TODO: get background thread count from json
  int backgroudThreadCount = 0;
  mV8Platform = kj::Own<v8::Platform>(workerd::jsg::defaultPlatform(backgroudThreadCount));
  mKJPlatform = kj::heap<WorkerdPlatform>(*mV8Platform);

  // TODO: XXXX
  // jsg::V8System v8System(v8Platform,
  //     KJ_MAP(flag, config.getV8Flags()) -> kj::StringPtr { return flag; });
  mV8System = kj::heap<workerd::jsg::V8System>(*mKJPlatform);

  mInitialed = true;
  return kj::str("{\"code\":0}");
}

kj::String WorkerdGoRuntime::onCeateWorkerd(const kj::String& args) {
  capnp::MallocMessageBuilder inputMessage;
  capnp::JsonCodec json;
  json.handleByAnnotation<gox::CreateWorkerdInput>();
  kj::StringPtr input = args;
  auto msgBuilder = inputMessage.initRoot<gox::CreateWorkerdInput>();
  try {
    json.decode(input, msgBuilder);
  } catch (kj::Exception& ex) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", ex.getDescription(),"\"}");
    return kjstr;
  }

  auto id = kj::str(msgBuilder.getId());

  // check if exists
  auto entry = mWorkerds.findEntry(id);
  if (entry != kj::none) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "workerd with id:", id, " already exists", "\"}");
    return kjstr;
  }

  auto addr = msgBuilder.getSocketAddr();
  if (addr == nullptr || addr.size() == 0) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "workerd must provide socket addr", "\"}");
    return kjstr;
  }

  auto workerd = kj::heap<GoWorkerd>(msgBuilder, *mV8System);
  if (0 != workerd->init()) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "create workerd failed:", workerd->error(), "\"}");
    return kjstr;
  }

  mWorkerds.insert(kj::mv(id), kj::mv(workerd));

  return kj::str("{\"code\":0}");
}

kj::String WorkerdGoRuntime::onDestroyWorkerd(const kj::String& args) {
  capnp::MallocMessageBuilder inputMessage;
  capnp::JsonCodec json;
  json.handleByAnnotation<gox::DestroyWorkerdInput>();
  kj::StringPtr input = args;
  auto msgBuilder = inputMessage.initRoot<gox::DestroyWorkerdInput>();
  try {
    json.decode(input, msgBuilder);
  } catch (kj::Exception& ex) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", ex.getDescription(),"\"}");
    return kjstr;
  }

  auto id = kj::str(msgBuilder.getId());

  // check if exists
  auto entry = mWorkerds.findEntry(id);
  auto& ee = KJ_UNWRAP_OR(entry, {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "workerd with id:", id, " not exists", "\"}");
    return kjstr;
  });

  auto& workerd = ee.value;
  if (0 != workerd->destroy()) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "workerd destroy failed:", workerd->error(), "\"}");
    return kjstr;
  }

  mWorkerds.erase(ee);

  return kj::str("{\"code\":0}");
}

kj::String WorkerdGoRuntime::onQueryWorkerd(const kj::String& args) {
  capnp::MallocMessageBuilder inputMessage;
  capnp::JsonCodec json;
  json.handleByAnnotation<gox::QueryWorkerdInput>();
  kj::StringPtr input = args;
  auto msgBuilder = inputMessage.initRoot<gox::QueryWorkerdInput>();
  try {
    json.decode(input, msgBuilder);
  } catch (kj::Exception& ex) {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", ex.getDescription(),"\"}");
    return kjstr;
  }

  auto id = kj::str(msgBuilder.getId());

  // check if exists
  auto entry = mWorkerds.findEntry(id);
  auto& ee = KJ_UNWRAP_OR(entry, {
    auto kjstr = kj::str("{\"code\":-1,\"msg\":\"", "workerd with id:", id, " not exists", "\"}");
    return kjstr;
  });

  capnp::MallocMessageBuilder message;
  auto output = message.getRoot<gox::QueryWorkerdOutput>();
  output.setCode(0);
  auto error = ee.value->error();
  if (error.size() != 0) {
    output.setError(error);
    output.setState(1);
  } else {
    output.setState(0);
  }

  auto outputStr = json.encode(output);
  return outputStr;
}

void WorkerdGoRuntime::reportEvent(const kj::String& event) {
  if (mReportURL.size() == 0) {
    return;
  }

  try {
    auto io = kj::setupAsyncIo();
    kj::TimerImpl clientTimer(kj::origin<kj::TimePoint>());
    kj::HttpHeaderTable headerTable;
    auto client = kj::newHttpClient(clientTimer,
        headerTable, io.provider->getNetwork(), kj::none);

    auto expectedBodySize = event.size();
    kj::HttpHeaders headers(headerTable);
    auto request =  client->request(
        kj::HttpMethod::POST, mReportURL, headers, expectedBodySize);

    request.body->write(event.cStr(), expectedBodySize).wait(io.waitScope);
    auto response = request.response.wait(io.waitScope);

    if (response.statusCode != 200) {
      KJ_LOG(ERROR, "reportEvent ", "statusCode = ", response.statusCode);
    } else {
      //KJ_LOG(INFO, response.body->readAllText().wait(io.waitScope));
    }
  } catch (kj::Exception& ex) {
    KJ_LOG(ERROR, "reportEvent ", ex.getDescription());
  }
}

} // namespace workerd::server

void testInitRuntime(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto runtimeInput = message.getRoot<gox::InitRuntimeInput>();
  runtimeInput.setReportURL("http://127.0.0.1:3000/workerd_report");

  auto input2 = json.encode(runtimeInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("initRuntime");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("initRuntime result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

void testCreateWorkerd1(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto createWorkerdInput = message.getRoot<gox::CreateWorkerdInput>();
  createWorkerdInput.setDirectory("/home/abc/echoproj");
  createWorkerdInput.setConfigFile("config.capnp");
  createWorkerdInput.setId("af00b595-81fd-4602-aa36-6f49b912cec7");
  createWorkerdInput.setSocketAddr("127.0.0.1:8080");

  auto input2 = json.encode(createWorkerdInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("createWorkerd");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("createWorkerd result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

void testDestroyWorkerd1(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto destroyWorkerdInput = message.getRoot<gox::DestroyWorkerdInput>();
  destroyWorkerdInput.setId("af00b595-81fd-4602-aa36-6f49b912cec7");

  auto input2 = json.encode(destroyWorkerdInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("destroyWorkerd");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("destroyWorkerd result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

void testCreateWorkerd2(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto createWorkerdInput = message.getRoot<gox::CreateWorkerdInput>();
  createWorkerdInput.setDirectory("/home/abc/titan/workerd/samples/helloworld");
  createWorkerdInput.setConfigFile("config.capnp");
  createWorkerdInput.setId("af00b595-81fd-4602-aa36-6f49b912cec8");
  createWorkerdInput.setSocketAddr("127.0.0.1:8060");

  auto input2 = json.encode(createWorkerdInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("createWorkerd");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("createWorkerd result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

void testDestroyWorkerd2(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto destroyWorkerdInput = message.getRoot<gox::DestroyWorkerdInput>();
  destroyWorkerdInput.setId("af00b595-81fd-4602-aa36-6f49b912cec8");

  auto input2 = json.encode(destroyWorkerdInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("destroyWorkerd");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("destroyWorkerd result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

void testQueryWorkerd(void) {
  using namespace workerd::server;

  capnp::MallocMessageBuilder message;
  capnp::JsonCodec json;
  auto queryWorkerdInput = message.getRoot<gox::QueryWorkerdInput>();
  queryWorkerdInput.setId("af00b595-81fd-4602-aa36-6f49b912cec7");

  auto input2 = json.encode(queryWorkerdInput);

  auto jcInput = message.getRoot<gox::JSONCallInput>();
  jcInput.setMethod("queryWorkerd");
  jcInput.setArgs(input2);

  auto inputStr = json.encode(jcInput);
  auto input = inputStr.cStr();
  const char* ptrResult = workerdGoRuntimeJsonCall(input);
  int length = strnlen(ptrResult, 256);

  KJ_LOG(INFO, kj::str("queryWorkerd result: ", ptrResult, ", len:", length));
  workerdGoFreeHeapStrPtr(ptrResult);
}

int main(int argc, char* argv[]) {
  kj::_::Debug::setLogLevel(kj::LogSeverity::INFO);

  testInitRuntime();

  while(true) {
    std::string in;
    std::cin >> in;

    if (in == "c1") {
      testCreateWorkerd1();
    } else if (in == "c2") {
      testCreateWorkerd2();
    } else if (in == "d1") {
      testDestroyWorkerd1();
    } else if (in == "d2") {
      testDestroyWorkerd2();
    } else if (in == "q1") {
      testQueryWorkerd();
    } else if (in == "quit") {
      break;
    } else {
      std::cout << "unknown cmd:" << in << std::endl;
    }
  }

  return 0;
}
