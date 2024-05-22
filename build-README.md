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
