#pragma once
#include <kj/filesystem.h>

namespace workerd::server {

kj::Own<kj::Filesystem> newGoDiskFilesystem(kj::StringPtr root);

} // namespace workerd::server
