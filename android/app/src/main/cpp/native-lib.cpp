#include <jni.h>
#include <string>
#include "src/workerd/server/cgo.h"


int workerMain(JNIEnv* env, jobject obj, jobjectArray args) {
    int argc = env->GetArrayLength(args);
    char* argv[argc];

    // 将Java字符串数组转换为char*数组
    for (int i = 0; i < argc; ++i) {
        jstring arg = (jstring)env->GetObjectArrayElement(args, i);
        const char* argStr = env->GetStringUTFChars(arg, nullptr);
        argv[i] = const_cast<char*>(argStr);
    }

    // 调用workerd_main函数
    int result = workerd_main(argc, argv);

    // 释放字符串数组
    for (int i = 0; i < argc; ++i) {
        jstring arg = (jstring)env->GetObjectArrayElement(args, i);
        env->ReleaseStringUTFChars(arg, argv[i]);
    }

    return result;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_example_android_bazel_MainActivity_workerdMainJNI(
        JNIEnv *env,
        jobject obj,  jobjectArray args) {
    int ret = workerMain(env, obj, args);
    std::string hello = "Hello from C++, ret: " + std::to_string(ret);
    return env->NewStringUTF(hello.c_str());
}


