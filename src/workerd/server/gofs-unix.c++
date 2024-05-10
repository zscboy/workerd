#include <kj/exception.h>
#include <kj/memory.h>
#include <kj/debug.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gofs.h"

// Prefix for temp files which should be hidden when listing a directory.
//
// If you change this, make sure to update the unit test.

#ifdef O_CLOEXEC
#define MAYBE_O_CLOEXEC O_CLOEXEC
#else
#define MAYBE_O_CLOEXEC 0
#endif

#ifdef O_DIRECTORY
#define MAYBE_O_DIRECTORY O_DIRECTORY
#else
#define MAYBE_O_DIRECTORY 0
#endif

namespace workerd::server {
using namespace kj;

class GoDirectory final : public kj::Directory {

public:
  GoDirectory(kj::StringPtr root): rootDir(kj::str(root)) {}

  Maybe<int> getFd() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory getFd not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Own<const FsNode> cloneFsNode() const override {
     kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory cloneFsNode not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Metadata stat() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory stat not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  void sync() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory sync not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  void datasync() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory datasync not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Array<String> listNames() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory listNames not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Array<Entry> listEntries() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory listEntries not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  bool exists(PathPtr path) const override {
        kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory exists not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Maybe<FsNode::Metadata> tryLstat(PathPtr path) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryLstat not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Maybe<Own<const ReadableFile>> tryOpenFile(PathPtr path) const override {
    int newFd;
    kj::String pathStr = path.toString();
    if (!pathStr.startsWith("/")) {
      pathStr = kj::str("/", path.toString());
    }

    KJ_SYSCALL_HANDLE_ERRORS(newFd = open(pathStr.cStr(), O_RDONLY | MAYBE_O_CLOEXEC)) {
      case ENOENT:
      case ENOTDIR:
        return kj::none;
      default:
        KJ_FAIL_SYSCALL("open(path, O_RDONLY)", error, path) { return kj::none; }
    }

    kj::AutoCloseFd result(newFd);
#ifndef O_CLOEXEC
    setCloexec(result);
#endif

    return newDiskReadableFile(kj::mv(result));
  }

  Maybe<Own<const ReadableDirectory>> tryOpenSubdir(PathPtr path) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryOpenSubdir not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Maybe<String> tryReadlink(PathPtr path) const override {
        kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryReadlink not implemented");
    kj::throwFatalException(kj::mv(e));
   }

  Maybe<Own<const File>> tryOpenFile(PathPtr path, WriteMode mode) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryOpenFile not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Own<Replacer<File>> replaceFile(PathPtr path, WriteMode mode) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory replaceFile not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Own<const File> createTemporary() const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory createTemporary not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Maybe<Own<AppendableFile>> tryAppendFile(PathPtr path, WriteMode mode) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryAppendFile not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Maybe<Own<const Directory>> tryOpenSubdir(PathPtr path, WriteMode mode) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory tryOpenSubdir not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  Own<Replacer<Directory>> replaceSubdir(PathPtr path, WriteMode mode) const override {
    kj::Exception e = KJ_EXCEPTION(FAILED, "GoDirectory replaceSubdir not implemented");
    kj::throwFatalException(kj::mv(e));
  }

  bool trySymlink(PathPtr linkpath, StringPtr content, WriteMode mode) const override {
    return false;
  }

  bool tryTransfer(PathPtr toPath, WriteMode toMode,
                   const Directory& fromDirectory, PathPtr fromPath,
                   TransferMode mode) const override {
    return false;
  }

  // tryTransferTo() not implemented because we have nothing special we can do.
  bool tryRemove(PathPtr path) const override {
    return false;
  }

private:
  kj::String rootDir;
};

class GoDiskFilesystem final: public kj::Filesystem {
public:
  GoDiskFilesystem(kj::StringPtr root):rootDir(root) {
    rootPath = kj::str(root);
  }

  const kj::Directory& getRoot() const override {
    return rootDir;
  }

  const kj::Directory& getCurrent() const override {
    return rootDir;
  }

  kj::PathPtr getCurrentPath() const override {
    return kj::Path(nullptr);
  }

private:
  // DiskDirectory root;
  // DiskDirectory current;
  GoDirectory rootDir;
  kj::String rootPath;
};

kj::Own<kj::Filesystem> newGoDiskFilesystem(kj::StringPtr root) {
  return kj::heap<GoDiskFilesystem>(root);
}

} // namespace workerd::server
