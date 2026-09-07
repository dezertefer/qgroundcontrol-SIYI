package org.mavlink.qgroundcontrol;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class BootReceiver extends BroadcastReceiver
{
    private static final String TAG = "QGC_BootReceiver";
    private static final String ACTION_QUICKBOOT_POWERON = "android.intent.action.QUICKBOOT_POWERON";

    @Override
    public void onReceive(Context context, Intent intent)
    {
        if (intent == null) {
            return;
        }

        final String action = intent.getAction();
        if (!Intent.ACTION_LOCKED_BOOT_COMPLETED.equals(action) &&
            !Intent.ACTION_BOOT_COMPLETED.equals(action) &&
            !ACTION_QUICKBOOT_POWERON.equals(action)) {
            return;
        }

        final Intent launchIntent = new Intent();
        launchIntent.setClassName(context.getPackageName(), "org.mavlink.qgroundcontrol.QGCActivity");
        launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                              Intent.FLAG_ACTIVITY_CLEAR_TOP |
                              Intent.FLAG_ACTIVITY_SINGLE_TOP);

        try {
            Log.i(TAG, "Starting FishMaps after " + action);
            context.startActivity(launchIntent);
        } catch (RuntimeException exception) {
            Log.e(TAG, "Unable to start FishMaps after boot", exception);
        }
    }
}
