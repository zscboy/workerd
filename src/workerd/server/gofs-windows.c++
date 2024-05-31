#include <kj/exception.h>
#include <kj/memory.h>
#include <kj/debug.h>
#include <kj/encoding.h>

#include <windows.h>
#include <winioctl.h>

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
    auto pathStr2 = kj::str(rootDir,  "\\", path);
    // KJ_LOG(INFO, kj::str("pathStr2: ", kj::str(pathStr2)));
    // auto pathStr2 = kj::str(path);
    auto pathArray = kj::encodeWideString(kj::str(pathStr2), true); // convert to wchar_t
    for (auto c: pathArray) {
      if (c == L'/') {
        c = L'\\';
      }
    }

    HANDLE newHandle;
    KJ_WIN32_HANDLE_ERRORS(newHandle = CreateFileW(
        pathArray.begin(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL)) {
      case ERROR_FILE_NOT_FOUND:
      case ERROR_PATH_NOT_FOUND:
        return nullptr;
      default:
        KJ_FAIL_WIN32("CreateFile(path, OPEN_EXISTING)", error, path) { return nullptr; }
    }

    return newDiskReadableFile(kj::AutoCloseHandle(newHandle));
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
  GoDiskFilesystem(kj::StringPtr root, kj::StringPtr drvie):rootDir(drvie) {
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
     auto path = kj::Path(nullptr);
     path = path.evalNative(kj::str(root));
     auto drive =  path.slice(0, 1);
     auto relativePath =  path.slice(1, path.size());
     return kj::heap<GoDiskFilesystem>(relativePath.toString(), drive.toString());
}

} // namespace workerd::server
