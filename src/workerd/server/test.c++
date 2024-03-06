// Copyright (c) 2017-2022 Cloudflare, Inc.
// Licensed under the Apache 2.0 license found in the LICENSE file or at:
//     https://opensource.org/licenses/Apache-2.0

#include "server.h"
#include <kj/test.h>
#include <workerd/util/capnp-mock.h>
#include <workerd/jsg/setup.h>
// #include <kj/async-queue.h>
#include <regex>
#include <stdlib.h>
#include <iostream>


namespace workerd::server {
namespace {

#define KJ_FAIL_EXPECT_AT(location, ...) \
  KJ_LOG_AT(ERROR, location, ##__VA_ARGS__);
#define KJ_EXPECT_AT(cond, location, ...) \
  if (auto _kjCondition = ::kj::_::MAGIC_ASSERT << cond); \
  else KJ_FAIL_EXPECT_AT(location, "failed: expected " #cond, _kjCondition, ##__VA_ARGS__)

jsg::V8System v8System;
kj::AsyncIoContext io = kj::setupAsyncIo();
// This can only be created once per process, so we have to put it at the top level.

const bool verboseLog = ([]() {
  // TODO(beta): Improve uncaught exception reporting so that we dontt have to do this.
  kj::_::Debug::setLogLevel(kj::LogSeverity::INFO);
  return true;
})();

kj::Own<config::Config::Reader> parseConfig(kj::StringPtr text, kj::SourceLocation loc) {
  capnp::MallocMessageBuilder builder;
  auto root = builder.initRoot<config::Config>();
  KJ_IF_SOME(exception, kj::runCatchingExceptions([&]() {
    TEXT_CODEC.decode(text, root);
  })) {
    KJ_FAIL_REQUIRE_AT(loc, exception);
  }

  return capnp::clone(root.asReader());
}

// Accept an indented block of text and remove the indentation. From each line of text, this will
// remove a number of spaces up to the indentation of the first line.
//
// This is intended to allow multi-line raw text to be specified conveniently using C++11
// `R"(blah)"` literal syntax, without the need to mess up indentation relative to the
// surrounding code.
kj::String operator "" _blockquote(const char* str, size_t n) {
  kj::StringPtr text(str, n);

  // Ignore a leading newline so that `R"(` can be placed on the line before the initial indent.
  if (text.startsWith("\n")) {
    text = text.slice(1);
  }

  // Count indent size.
  size_t indent = 0;
  while (text.startsWith(" ")) {
    text = text.slice(1);
    ++indent;
  }

  // Process lines.
  kj::Vector<char> result;
  while (text != nullptr) {
    // Add data from this line.
    auto nl = text.findFirst('\n').orDefault(text.size() - 1) + 1;
    result.addAll(text.slice(0, nl));
    text = text.slice(nl);

    // Skip indent of next line, up to the expected indent size.
    size_t seenIndent = 0;
    while (seenIndent < indent && text.startsWith(" ")) {
      text = text.slice(1);
      ++seenIndent;
    }
  }

  result.add('\0');
  return kj::String(result.releaseAsArray());
}

class Test final: private kj::Filesystem, private kj::EntropySource, private kj::Clock {
public:
  Test(kj::StringPtr configText, kj::SourceLocation loc = {})
      : config(parseConfig(configText, loc)),
        root(kj::newInMemoryDirectory(*this)),
        pwd(kj::Path({"current", "dir"})),
        cwd(root->openSubdir(pwd, kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT)),
        timer(kj::origin<kj::TimePoint>()),
        server(*this, timer,  io.provider->getNetwork(), *this, Worker::ConsoleMode::INSPECTOR_ONLY,
                [this](kj::String error) {
          if (expectedErrors.startsWith(error) && expectedErrors[error.size()] == '\n') {
            expectedErrors = expectedErrors.slice(error.size() + 1);
          } else {
            KJ_FAIL_EXPECT(error, expectedErrors);
          }
        }),
        fakeDate(kj::UNIX_EPOCH) {}
        // mockNetwork(*this, {}, {}) {}

  ~Test() noexcept(false) {
    // for (auto& subq: subrequests) {
    //   subq.value->rejectAll(KJ_EXCEPTION(FAILED, "test ended"));
    // }

    if (!unwindDetector.isUnwinding()) {
      // Make sure any errors are reported.
      KJ_IF_SOME(t, runTask) {
        t.poll(io.waitScope);
      }
    }
  }

  // Start the server. Call before connect().
  void start(kj::Promise<void> drainWhen = kj::NEVER_DONE) {
    KJ_REQUIRE(runTask == kj::none);
    auto task = server.run(v8System, *config, kj::mv(drainWhen))
        .eagerlyEvaluate([](kj::Exception&& e) {
      KJ_FAIL_EXPECT(e);
    });
    KJ_EXPECT(!task.poll(io.waitScope));
    runTask = kj::mv(task);
  }

    // Start the server. Call before connect().
  void run() {
      auto promise = server.run(v8System, *config);
      promise.wait(io.waitScope);
  }

  // Call instead of `start()` when the config is expected to produce errors. The parameter is
  // the expected list of errors messages, one per line.
  void expectErrors(kj::StringPtr expected) {
    expectedErrors = expected;
    server.run(v8System, *config).poll(io.waitScope);
    KJ_EXPECT(expectedErrors == nullptr, "some expected errors weren't seen");
  }

  // Advance the timer through `seconds` seconds of virtual time.
  void wait(size_t seconds) {
    auto delayPromise = timer.afterDelay(seconds * kj::SECONDS).eagerlyEvaluate(nullptr);
    while (!delayPromise.poll(io.waitScope)) {
      // Since this test has no external I/O at all other than time, we know no events could
      // possibly occur until the next timer event. So just advance directly to it and continue.
      timer.advanceTo(KJ_ASSERT_NONNULL(timer.nextEvent()));
    }
    delayPromise.wait(io.waitScope);
  }

  // kj::EventLoop loop;
  // kj::WaitScope ws = io.waitScope;

  kj::Own<config::Config::Reader> config;
  kj::Own<const kj::Directory> root;
  kj::Path pwd;
  kj::Own<const kj::Directory> cwd;
  kj::TimerImpl timer;
  Server server;

  kj::Maybe<kj::Promise<void>> runTask;
  kj::StringPtr expectedErrors;

  kj::Date fakeDate;

private:
  kj::UnwindDetector unwindDetector;

  // ---------------------------------------------------------------------------
  // implements Filesytem

  const kj::Directory& getRoot() const override {
    return *root;
  }
  const kj::Directory& getCurrent() const override {
    return *cwd;
  }
  kj::PathPtr getCurrentPath() const override {
    return pwd;
  }

  // ---------------------------------------------------------------------------
  // implements Network

  // Addresses that the server is listening on.
  kj::HashMap<kj::String, kj::Own<kj::NetworkAddress>> sockets;


  static kj::String peerFilterToString(kj::ArrayPtr<const kj::StringPtr> allow,
                                       kj::ArrayPtr<const kj::StringPtr> deny) {
    if (allow == nullptr && deny == nullptr) {
      return kj::str("(none)");
    } else {
      return kj::str(
          "allow: [", kj::strArray(allow, ", "), "], "
          "deny: [", kj::strArray(deny, ", "), "]");
    }
  }

  // ---------------------------------------------------------------------------
  // implements EntropySource

  void generate(kj::ArrayPtr<kj::byte> buffer) override {
    kj::byte random = 4;  // chosen by fair die roll by Randall Munroe in 2007.
                          // guaranteed to be random.
    memset(buffer.begin(), random, buffer.size());
  }

  // ---------------------------------------------------------------------------
  // implements Clock

  kj::Date now() const override {
    return fakeDate;
  }
};

// =======================================================================================

// TODO(beta): Test TLS (send and receive)
// TODO(beta): Test CLI overrides

}  // namespace
}  // namespace workerd::server


kj::String singleWorker(kj::StringPtr def) {
  return kj::str(R"((
    services = [
      ( name = "hello",
        worker = )"_kj, def, R"(
      )
    ],
    sockets = [
      ( name = "http",
        address = "*:8080",
        service = "hello"
      )
    ]
  ))"_kj);

}


#ifdef __cplusplus
extern "C" {
#endif

int test_workerd() {

    workerd::server::Test test(singleWorker(R"((
    compatibilityDate = "2022-08-17",
    serviceWorkerScript =
        `addEventListener("fetch", event => {
        `  event.respondWith(new Response("Hello " + event.request.url + "\n"));
        `})
    ))"_kj));
    test.run();

}


#ifdef __cplusplus
}
#endif

