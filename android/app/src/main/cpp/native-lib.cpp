#include <jni.h>
#include <string>
#include "src/workerd/server/cgo.h"
#include <android/log.h>

extern "C" JNIEXPORT jstring JNICALL Java_com_example_android_bazel_MyService_workerdMainJNI(
        JNIEnv *env,
        jobject /* this */) {


    //char arg2[] = "/sdcard/helloworld/config.capnp";
    //argv[2] = arg2;
    int ret = test_workerd();
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }


     __android_log_print(ANDROID_LOG_DEBUG, "com.example.android.baze", "JNI OnLoad");
     return JNI_VERSION_1_6;
}
