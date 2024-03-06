package com.example.android.bazel;

import android.app.Service; 
import android.content.Intent; 
import android.os.IBinder; 
import android.util.Log;

public class MyService extends Service {

   static {
        System.loadLibrary("app");
   }


   @Override
    public void onCreate() {
        super.onCreate();
        Log.d("MyService", "onCreate: Service created");

       // System.loadLibrary("app");
    } 

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
	String ret = workerdMainJNI();
	Log.d("MyService", "onStartCommand"+ret);
        return 0;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
    }


    public native String workerdMainJNI();
}
