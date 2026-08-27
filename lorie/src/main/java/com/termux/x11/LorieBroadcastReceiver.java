package com.termux.x11;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class LorieBroadcastReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String tag = CmdEntryPoint.ACTION_START.equals(intent.getAction())
                ? intent.getStringExtra(CmdEntryPoint.EXTRA_DOCUMENT_TAG) : null;
        MainActivity activity = MainActivity.getInstance(tag);
        if (activity != null)
            activity.onBroadcastReceive(context, intent);
        else
            Log.w("LorieBroadcastReceiver", "Got " + intent.getAction() + " but no MainActivity instance in this process");
    }
}
