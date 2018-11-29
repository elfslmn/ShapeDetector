package com.esalman17.shapedetector;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.ActivityInfo;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.graphics.Bitmap;
import android.graphics.Point;
import android.view.Display;

import java.util.HashMap;
import java.util.Iterator;

enum Mode{
    CAMERA,
    TEST,
}

public class MainActivity extends Activity {

    static {
        System.loadLibrary("usb_android");
        System.loadLibrary("royale");
        System.loadLibrary("nativelib");
    }

    private PendingIntent mUsbPi;
    private UsbManager manager;
    private UsbDeviceConnection usbConnection;

    private Bitmap bmpCam = null;
    private Bitmap bmpTest = null;
    private ImageView mainImView;

    boolean m_opened;
    Mode currentMode;

    private static final String LOG_TAG = "MainActivity";
    private static final String ACTION_USB_PERMISSION = "ACTION_ROYALE_USB_PERMISSION";

    int scaleFactor;
    int[] resolution;
    Point displaySize, camRes;

    public native int[] OpenCameraNative(int fd, int vid, int pid);
    public native void CloseCameraNative();
    public native void RegisterCallback();
    public native void DetectBackgroundNative();
    public native void ChangeModeNative(int mode);

    //broadcast receiver for user usb permission dialog
    private final BroadcastReceiver mUsbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (ACTION_USB_PERMISSION.equals(action)) {
                UsbDevice device = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);

                if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                    if (device != null) {
                        RegisterCallback();
                        performUsbPermissionCallback(device);
                        createBitmap();
                    }
                } else {
                    System.out.println("permission denied for device" + device);
                }
            }
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setRequestedOrientation (ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        setContentView(R.layout.activity_main);
        Log.d(LOG_TAG, "onCreate()");

        // hide the navigation bar
        final int flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;

        getWindow().getDecorView().setSystemUiVisibility(flags);
        final View decorView = getWindow().getDecorView();
        decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility)
            {
                if((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0)
                {
                    decorView.setSystemUiVisibility(flags);
                }
            }
        });

        Button btnCamera= findViewById(R.id.buttonCamera);
        mainImView =  findViewById(R.id.imageViewMain);
        btnCamera.setOnClickListener(new View.OnClickListener() {

            @Override
            public void onClick(View view) {
                openCamera();
                ChangeModeNative(1);
                currentMode = Mode.CAMERA;
            }
        });
        findViewById(R.id.buttonBackGr).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if (!m_opened) {
                    openCamera();
                }
                if(currentMode == null){
                    ChangeModeNative(1);
                    currentMode = Mode.CAMERA;
                }
                DetectBackgroundNative();
            }
        });
        findViewById(R.id.buttonTest).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                ChangeModeNative(2);
                currentMode = Mode.TEST;
            }
        });
    }

    @Override
    protected void onPause() {
        if (m_opened) {
            CloseCameraNative();
            m_opened = false;
        }
        super.onPause();
        Log.d(LOG_TAG, "onPause()");
        unregisterReceiver(mUsbReceiver);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.d(LOG_TAG, "onResume()");
        registerReceiver(mUsbReceiver, new IntentFilter(ACTION_USB_PERMISSION));
    }

    @Override
    protected void onDestroy() {
        Log.d(LOG_TAG, "onDestroy()");
        //unregisterReceiver(mUsbReceiver);

        if(usbConnection != null) {
            usbConnection.close();
        }

        super.onDestroy();
    }

    public void openCamera() {
        Log.d(LOG_TAG, "openCamera");

        //check permission and request if not granted yet
        manager = (UsbManager) getSystemService(Context.USB_SERVICE);

        if (manager != null) {
            Log.d(LOG_TAG, "Manager valid");
        }

        HashMap<String, UsbDevice> deviceList = manager.getDeviceList();

        Log.d(LOG_TAG, "USB Devices : " + deviceList.size());

        Iterator<UsbDevice> iterator = deviceList.values().iterator();
        UsbDevice device;
        boolean found = false;
        while (iterator.hasNext()) {
            device = iterator.next();
            if (device.getVendorId() == 0x1C28 ||
                    device.getVendorId() == 0x058B ||
                    device.getVendorId() == 0x1f46) {
                Log.d(LOG_TAG, "royale device found");
                found = true;
                if (!manager.hasPermission(device)) {
                    Intent intent = new Intent(ACTION_USB_PERMISSION);
                    intent.setAction(ACTION_USB_PERMISSION);
                    mUsbPi = PendingIntent.getBroadcast(this, 0, intent, 0);
                    manager.requestPermission(device, mUsbPi);
                } else {
                    RegisterCallback();
                    performUsbPermissionCallback(device);
                    createBitmap();
                }
                break;
            }
        }
        if (!found) {
            Log.e(LOG_TAG, "No royale device found!!!");
        }
    }

    private void performUsbPermissionCallback(UsbDevice device) {
        usbConnection = manager.openDevice(device);
        Log.i(LOG_TAG, "permission granted for: " + device.getDeviceName() + ", fileDesc: " + usbConnection.getFileDescriptor());

        int fd = usbConnection.getFileDescriptor();

        resolution = OpenCameraNative(fd, device.getVendorId(), device.getProductId());
        camRes = new Point(resolution[0], resolution[1]);

        if (resolution[0] > 0) {
            m_opened = true;
        }
    }

    private void createBitmap() {

        // calculate scale factor, which scales the bitmap relative to the display resolution
        Display display = getWindowManager().getDefaultDisplay();
        Point size = new Point();
        display.getSize(size);
        double displayWidth = size.x * 0.9;
        scaleFactor = (int) displayWidth / resolution[0];

        if (bmpCam == null) {
            bmpCam = Bitmap.createBitmap(resolution[0], resolution[1], Bitmap.Config.ARGB_8888);
        }
    }

    public void amplitudeCallback(int[] amplitudes) {
        if (!m_opened)
        {
            Log.d(LOG_TAG, "Device in Java not initialized");
            return;
        }
        bmpCam.setPixels(amplitudes, 0, resolution[0], 0, 0, resolution[0], resolution[1]);

        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mainImView.setImageBitmap(bmpCam);
            }
        });
    }
    // TEST MODE FUNCTIONS ---------------------------------------------------------------------------------
    private void initializeTestMode(){
        if (bmpTest == null) {
            displaySize = new Point();
            getWindowManager().getDefaultDisplay().getRealSize(displaySize);
            Log.i(LOG_TAG, "Window display size: x=" + displaySize.x + ", y=" + displaySize.y);
            bmpTest = Bitmap.createBitmap(displaySize.x, displaySize.y, Bitmap.Config.ARGB_8888);
        }
    }

    public void shapeDetectedCallback(int[] descriptors){
        if (!m_opened)
        {
            Log.d(LOG_TAG, "Device in Java not initialized");
            return;
        }

    }

    // depends on camera position, not a generic function TODO make generic
    private Point convertCamPixel2ProPixel(float x, float y, float z){
        if( x<0 || y<0 || z<=0){
            return null;
        }
        if(displaySize == null || camRes == null){
            return null;
        }

        //scale = sin(camFov/2) / sin(proFov/2)
        double scale_x = 1.3074;
        double scale_y = 1.8256;
        double shifty = 486.69004 * Math.exp(-0.048035356*z);
        double px = x * displaySize.x * scale_x / camRes.x  - displaySize.x*(scale_x -1) /2 ;  // shiftx nearly 0
        double py = y * displaySize.y * scale_y / camRes.y  - displaySize.y*(scale_y -1)/2 - shifty;

        if(px > displaySize.x || px < 0 || py>displaySize.y || py < 0){
            Log.d(LOG_TAG, "Point is outside of the projector view");
            return null;
        }
        return  new Point((int)px, (int)py );
    }


}

