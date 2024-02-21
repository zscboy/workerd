package main

/*
#cgo CFLAGS: -I.
#include <stdlib.h>
#include "cgo.h"
*/
import "C"
import (
	"fmt"
	"os"
	"unsafe"
)

// WorkerdMain 是Go中调用workerd_main C函数的封装
func WorkerdMain(args []string) int {
	cArgs := make([]*C.char, len(args))
	for i, arg := range args {
		cArgs[i] = C.CString(arg)
		defer C.free(unsafe.Pointer(cArgs[i]))
	}

	return int(C.workerd_main(C.int(len(args)), &cArgs[0]))
}

func main() {
	fmt.Println("this is run in go")
	exitCode := WorkerdMain(os.Args)
	os.Exit(exitCode)
}
