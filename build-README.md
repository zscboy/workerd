build android64:
  bazelisk build //src/workerd/server:goworkerd --config=android64
  OR with thin-lto:
  bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=android64

  note: need ndk installed

build android32:
  bazelisk build //src/workerd/server:goworkerd --config=android32
  OR with thin-lto:
  bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=android32

  note: need ndk installed
  note: build android 32 need gcc-multilib in host linux platform:
    sudo apt-get install -y gcc-multilib

build android_x86-64:
  bazelisk build //src/workerd/server:goworkerd --config=androidx64
  OR with thin-lto:
  bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=androidx64

  note: need ndk installed

build linux64:
  bazelisk build //src/workerd/server:goworkerd --config=linux64
  OR with thin-lto:
  bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=linux64

build windows64:
  bazelisk build //src/workerd/server:goworkerd --config=win64
  OR with thin-lto:
  bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=win64

build macos64:
bazelisk build //src/workerd/server:goworkerdx --config=macos64
OR with thin-lto:
bazelisk build //src/workerd/server:goworkerd --config=thin-lto --config=macos64

v8 mksnapshots (run on target system)
   # see v8/src/snapshot/embedded/platform-embedded-file-writer-base.cc for --target_os and --target_arch options
   # on android, adb push mksnapshot executable to phone's /data/local/tmp/ directory, then chmod a+x, then run it
   # note that android64 phone can run android32 executable
  ./mksnapshot --embedded_variant=Default --startup_src=snapshot.cc --embedded_src=embedded.S

