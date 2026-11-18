package io.github.alondero.kdc;

import android.os.Bundle;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.View;
import android.view.KeyEvent;
import android.view.ViewGroup;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private TouchController controls;
    private InputManager inputs;
    @Override protected String[] getLibraries() { return new String[]{"main"}; }
    @Override protected String[] getArguments() {
        return new String[]{"--no-launcher", new File(getFilesDir(), ImportActivity.ROM).getAbsolutePath()};
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK &&
                !event.isFromSource(android.view.InputDevice.SOURCE_GAMEPAD)) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) finish();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override protected void onDestroy() {
        super.onDestroy(); // SDL sends quit and waits for the game thread.
        // Each session needs fresh native globals. The import screen owns a separate process.
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (mLayout == null) return;
        controls = new TouchController(this);
        mLayout.addView(controls, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        inputs = getSystemService(InputManager.class);
        updateControls();
    }

    @Override protected void onResume() {
        super.onResume();
        if (controls == null) return;
        inputs.registerInputDeviceListener(this, null);
        updateControls();
    }

    @Override protected void onPause() {
        if (controls != null) {
            inputs.unregisterInputDeviceListener(this);
            controls.release();
        }
        super.onPause();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        if (!focused && controls != null) controls.release();
        super.onWindowFocusChanged(focused);
    }

    private void updateControls() {
        for (int id : inputs.getInputDeviceIds()) {
            InputDevice device = inputs.getInputDevice(id);
            if (device != null && !device.isVirtual() &&
                    (device.supportsSource(InputDevice.SOURCE_GAMEPAD) ||
                     device.supportsSource(InputDevice.SOURCE_JOYSTICK))) {
                controls.release();
                controls.setVisibility(View.GONE);
                return;
            }
        }
        controls.setVisibility(View.VISIBLE);
    }

    @Override public void onInputDeviceAdded(int id) { updateControls(); }
    @Override public void onInputDeviceChanged(int id) { updateControls(); }
    @Override public void onInputDeviceRemoved(int id) { updateControls(); }
}
