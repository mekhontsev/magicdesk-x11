package com.termux.x11;

import android.os.IBinder;

// This interface is used by utility on termux side.
interface ICmdEntryInterface {
    ParcelFileDescriptor getXConnection();
    ParcelFileDescriptor getLogcatOutput();
    void retain(IBinder owner);
    oneway void stop();
}
