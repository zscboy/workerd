load("@io_bazel_rules_go//go:def.bzl", "go_binary", "go_library")
go_binary(
    name = "test",
    srcs = ["main.go"],
    cdeps = ["//src/workerd/server:libworkerd"],
    cgo = True,
)

