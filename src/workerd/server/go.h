#pragma once
#include <kj/mutex.h>
#include <workerd/jsg/setup.h>
#include "v8-platform-impl.h"
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <capnp/schema-parser.h>
#include <capnp/dynamic.h>
#include <workerd/server/workerd.capnp.h>
#include <workerd/server/workerd-meta.capnp.h>
#include <workerd/server/gox.capnp.h>
#include <kj/filesystem.h>
#include <kj/async-io.h>
#include <kj/async-queue.h>
#include <kj/compat/http.h>
#include <openssl/rand.h>

#include "gofs.h"

#if _WIN32
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
  // golang call to init workerd runtime
  DLL_EXPORT const char* workerdGoRuntimeJsonCall(const char* jsonString);
  DLL_EXPORT void workerdGoFreeHeapStrPtr(const char* ptr);
}

namespace workerd::server {

class EntropySourceImpl: public kj::EntropySource {
public:
  void generate(kj::ArrayPtr<kj::byte> buffer) override {
    KJ_ASSERT(RAND_bytes(buffer.begin(), buffer.size()) == 1);
  }
};

// =======================================================================================

// A kj::Network implementation which wraps some other network and optionally (if enabled)
// implements "loopback:" network addresses, which are expected to be serviced within the same
// process. Loopback addresses are enabled only when running `workerd test`. The purpose is to
// allow end-to-end testing of the network stack without creating a real external-facing socket.
//
// There is no use for loopback sockets in production since direct service bindings are more
// efficient while solving the same problems.
class NetworkWithLoopback final: public kj::Network {
public:
  NetworkWithLoopback(kj::Network& inner, kj::AsyncIoProvider& ioProvider)
      : inner(inner), ioProvider(ioProvider), loopbackEnabled(rootLoopbackEnabled) {}

  NetworkWithLoopback(kj::Own<kj::Network> inner, kj::AsyncIoProvider& ioProvider,
                      bool& loopbackEnabled)
      : inner(*inner), ownInner(kj::mv(inner)), ioProvider(ioProvider),
        loopbackEnabled(loopbackEnabled) {}

  // Call once to enable loopback addresses.
  void enableLoopback() { loopbackEnabled = true; }

  kj::Promise<kj::Own<kj::NetworkAddress>> parseAddress(
      kj::StringPtr addr, uint portHint = 0) override {
    if (loopbackEnabled && addr.startsWith(PREFIX)) {
      return kj::Own<kj::NetworkAddress>(kj::heap<LoopbackAddr>(*this, addr.slice(PREFIX.size())));
    } else {
      return inner.parseAddress(addr, portHint);
    }
  }

  kj::Own<kj::NetworkAddress> getSockaddr(const void* sockaddr, uint len) override {
    return inner.getSockaddr(sockaddr, len);
  }

  kj::Own<kj::Network> restrictPeers(
      kj::ArrayPtr<const kj::StringPtr> allow,
      kj::ArrayPtr<const kj::StringPtr> deny = nullptr) override {
    return kj::heap<NetworkWithLoopback>(
        inner.restrictPeers(allow, deny), ioProvider, loopbackEnabled);
  }

private:
  kj::Network& inner;
  kj::Own<kj::Network> ownInner;
  kj::AsyncIoProvider& ioProvider KJ_UNUSED;
  bool rootLoopbackEnabled = false;

  // Reference to `rootLoopbackEnabled` of the root NetworkWithLoopback. All descendants
  // (created using `restrictPeers()` will share the same flag value.
  bool& loopbackEnabled;

  using ConnectionQueue = kj::ProducerConsumerQueue<kj::Own<kj::AsyncIoStream>>;
  kj::HashMap<kj::String, kj::Own<ConnectionQueue>> loopbackQueues;

  ConnectionQueue& getLoopbackQueue(kj::StringPtr name) {
    return *loopbackQueues.findOrCreate(name, [&]() {
      return decltype(loopbackQueues)::Entry {
        .key = kj::str(name),
        .value = kj::heap<ConnectionQueue>(),
      };
    });
  }

  static constexpr kj::StringPtr PREFIX = "loopback:"_kj;

  class LoopbackAddr final: public kj::NetworkAddress {
  public:
    LoopbackAddr(NetworkWithLoopback& parent, kj::StringPtr name)
        : parent(parent), name(kj::str(name)) {}

    kj::Promise<kj::Own<kj::AsyncIoStream>> connect() override {
      // The purpose of loopback sockets is to actually test the network stack end-to-end. If
      // people don't want to test the full stack, then they can create a direct service binding
      // without going through a loopback socket.
      //
      // So, we create a real loopback socket here.
      auto pipe = parent.ioProvider.newTwoWayPipe();

      parent.getLoopbackQueue(name).push(kj::mv(pipe.ends[0]));
      return kj::mv(pipe.ends[1]);
    }

    kj::Own<kj::ConnectionReceiver> listen() override {
      return kj::heap<LoopbackReceiver>(parent.getLoopbackQueue(name));
    }

    kj::Own<kj::NetworkAddress> clone() override {
      return kj::heap<LoopbackAddr>(parent, name);
    }

    kj::String toString() override {
      return kj::str(PREFIX, name);
    }

  private:
    NetworkWithLoopback& parent;
    kj::String name;
  };

  class LoopbackReceiver final: public kj::ConnectionReceiver {
  public:
    LoopbackReceiver(ConnectionQueue& queue): queue(queue) {}

    kj::Promise<kj::Own<kj::AsyncIoStream>> accept() override {
      return queue.pop();
    }

    uint getPort() override {
      return 0;
    }

  private:
    ConnectionQueue& queue;
  };
};

// =======================================================================================

kj::Maybe<kj::Own<capnp::SchemaFile>> tryImportBulitin(kj::StringPtr name);

// Callbacks for capnp::SchemaFileLoader. Implementing this interface lets us control import
// resolution, which we want to do mainly so that we can set watches on all imported files.
//
// These callbacks also give us more control over error reporting, in particular the ability
// to not throw an exception on the first error seen.
class SchemaFileImpl final: public capnp::SchemaFile {
public:
  class ErrorReporter {
  public:
    virtual void reportParsingError(kj::StringPtr file,
        SourcePos start, SourcePos end, kj::StringPtr message) = 0;
  };

  SchemaFileImpl(const kj::Directory& root, kj::PathPtr current,
                 kj::Path fullPathParam, kj::PathPtr basePath,
                 kj::ArrayPtr<const kj::Path> importPath,
                 kj::Own<const kj::ReadableFile> fileParam,
                 ErrorReporter& errorReporter)
      : root(root), current(current), fullPath(kj::mv(fullPathParam)), basePath(basePath),
        importPath(importPath), file(kj::mv(fileParam)),
        errorReporter(errorReporter) {
    if (fullPath.startsWith(current)) {
      // Simplify display name by removing current directory prefix.
      displayName = fullPath.slice(current.size(), fullPath.size()).toNativeString();
    } else {
      // Use full path.
      displayName = fullPath.toNativeString(true);
    }
  }

  kj::StringPtr getDisplayName() const override {
    return displayName;
  }

  kj::Array<const char> readContent() const override {
    return file->mmap(0, file->stat().size).releaseAsChars();
  }

  kj::Maybe<kj::Own<SchemaFile>> import(kj::StringPtr target) const override {
    if (target.startsWith("/")) {
      auto parsedPath = kj::Path::parse(target.slice(1));
      for (auto& candidate: importPath) {
        auto newFullPath = candidate.append(parsedPath);

        KJ_IF_SOME(newFile, root.tryOpenFile(newFullPath)) {
          return kj::implicitCast<kj::Own<SchemaFile>>(kj::heap<SchemaFileImpl>(
              root, current, kj::mv(newFullPath), candidate, importPath,
              kj::mv(newFile), errorReporter));
        }
      }
      // No matching file found. Check if we have a builtin.
      return tryImportBulitin(target);
    } else {
      auto relativeTo = fullPath.slice(basePath.size(), fullPath.size());
      auto parsed = relativeTo.parent().eval(target);
      auto newFullPath = basePath.append(parsed);

      KJ_IF_SOME(newFile, root.tryOpenFile(newFullPath)) {
        return kj::implicitCast<kj::Own<SchemaFile>>(kj::heap<SchemaFileImpl>(
            root, current, kj::mv(newFullPath), basePath, importPath, kj::mv(newFile),
            errorReporter));
      } else {
        return kj::none;
      }
    }
  }

  bool operator==(const SchemaFile& other) const override {
    if (auto downcasted = dynamic_cast<const SchemaFileImpl*>(&other)) {
      return fullPath == downcasted->fullPath;
    } else {
      return false;
    }
  }

  size_t hashCode() const override {
    return kj::hashCode(fullPath);
  }

  void reportError(SourcePos start, SourcePos end, kj::StringPtr message) const override {
    errorReporter.reportParsingError(displayName, start, end, message);
  }

private:
  const kj::Directory& root;
  kj::PathPtr current;

  // Full path from root of filesystem to the file.
  kj::Path fullPath;

  // If this file was reached by scanning `importPath`, `basePath` is the particular import path
  // directory that was used, otherwise it is empty. `basePath` is always a prefix of `fullPath`.
  kj::PathPtr basePath;

  // Paths to search for absolute imports.
  kj::ArrayPtr<const kj::Path> importPath;

  kj::Own<const kj::ReadableFile> file;
  kj::String displayName;

  ErrorReporter& errorReporter;
};

// A schema file whose text is embedded into the binary for convenience.
//
// TODO(someday): Could `capnp::SchemaParser` be updated such that it can use the compiled-in
//   schema nodes rather than re-parse the file from scratch? This is tricky as some information
//   is lost after compilation which is needed to compile dependents, e.g. aliases are erased.
class BuiltinSchemaFileImpl final: public capnp::SchemaFile {
public:
  BuiltinSchemaFileImpl(kj::StringPtr name, kj::StringPtr content)
      : name(name), content(content) {}

  kj::StringPtr getDisplayName() const override {
    return name;
  }

  kj::Array<const char> readContent() const override {
    return kj::Array<const char>(content.begin(), content.size(), kj::NullArrayDisposer::instance);
  }

  kj::Maybe<kj::Own<SchemaFile>> import(kj::StringPtr target) const override {
    return tryImportBulitin(target);
  }

  bool operator==(const SchemaFile& other) const override {
    if (auto downcasted = dynamic_cast<const BuiltinSchemaFileImpl*>(&other)) {
      return downcasted->name == name;
    } else {
      return false;
    }
  }

  size_t hashCode() const override {
    return kj::hashCode(name);
  }

  void reportError(SourcePos start, SourcePos end, kj::StringPtr message) const override {
    KJ_FAIL_ASSERT("parse error in built-in schema?", start.line, start.column, message);
  }

private:
  kj::StringPtr name;
  kj::StringPtr content;
};

class GoWorkerd: public SchemaFileImpl::ErrorReporter{
public:
  GoWorkerd(gox::CreateWorkerdInput::Builder& input, workerd::jsg::V8System& v8System):
    mId(kj::str(input.getId())), mDir(kj::str(input.getDirectory())),
    mConfigFile(kj::str(input.getConfigFile())),mAddr(kj::str(input.getSocketAddr())), mV8System(v8System),
    mServeThreadRunning(false) {
      mFS = newGoDiskFilesystem(input.getDirectory());
  }

  virtual ~GoWorkerd() {
    // network = nullptr;
    // io = nullptr;
    mFS = nullptr;
  }

  // load config
  int init();

  // free all resource
  int destroy();

  kj::StringPtr error() {
    return mError;
  }

private:
  void parseConfigFile(kj::StringPtr pathStr);
  config::Config::Reader getConfig();
  void serveImpl();
  void stopServeThread();
  void reportParsingError(kj::StringPtr file,
        capnp::SchemaFile::SourcePos start, capnp::SchemaFile::SourcePos end,
        kj::StringPtr message) override;
private:
  kj::String mId;
  kj::String mDir;
  kj::String mConfigFile;
  kj::String mAddr;
  workerd::jsg::V8System& mV8System;

  mutable bool mServeThreadRunning;
  kj::Maybe<const kj::Executor&> mExecutor;
  kj::Own<kj::PromiseFulfiller<void>> mAbortFulFill;

  kj::Own<kj::Filesystem> mFS;

  kj::Vector<kj::Path> mImportPath;
  capnp::SchemaParser mSchemaParser;
  capnp::ParsedSchema mParsedSchema;
  kj::Vector<capnp::ConstSchema> mTopLevelConfigConstants;
  kj::Maybe<config::Config::Reader> mConfig;

  kj::String mError;
};

class WorkerdGoRuntime {
private:
  WorkerdGoRuntime():mInitialed(false) {

  }

  ~WorkerdGoRuntime() {
    auto iter = mWorkerds.begin();
    while (iter != mWorkerds.end()) {
      iter->value->destroy();
      ++iter;
    }

    mWorkerds.clear();

    mV8System = nullptr;
    mV8Platform = nullptr;
    mKJPlatform = nullptr;
  }

public:
    WorkerdGoRuntime(const WorkerdGoRuntime&) = delete;
    WorkerdGoRuntime& operator=(const WorkerdGoRuntime &) = delete;
    WorkerdGoRuntime(WorkerdGoRuntime &&) = delete;
    WorkerdGoRuntime & operator=(WorkerdGoRuntime &&) = delete;

    // singleton
    static auto& instance(){
        static WorkerdGoRuntime _grInstance;
        return _grInstance;
    }

    kj::String  onJsonCall(const char* jsonString);

private:
    kj::String onInitRuntime(const kj::String& args);
    kj::String onCeateWorkerd(const kj::String& args);
    kj::String onDestroyWorkerd(const kj::String& args);
    kj::String onQueryWorkerd(const kj::String& args);

    void reportEvent(const kj::String& event);

private:
  bool mInitialed;
  kj::MutexGuarded<int> mLock;
  kj::String mReportURL;

  kj::HashMap<kj::String, kj::Own<GoWorkerd>> mWorkerds;

  kj::Own<workerd::jsg::V8System> mV8System;
  kj::Own<v8::Platform> mV8Platform;
  kj::Own<WorkerdPlatform> mKJPlatform;
};

} // namespace workerd::server
