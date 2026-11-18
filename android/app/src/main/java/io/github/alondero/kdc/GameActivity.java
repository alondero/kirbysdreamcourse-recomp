package io.github.alondero.kdc;

import android.graphics.Insets;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    static final String SHOW_LAUNCHER = "show_launcher";
    private TouchController controls;
    private FrameLayout menuOverlay;
    private InputManager inputs;
    private boolean gameplayControlsVisible;

    @Override protected String[] getLibraries() { return new String[]{"main"}; }
    @Override protected String[] getArguments() {
        String rom = new File(getFilesDir(), ImportActivity.ROM).getAbsolutePath();
        if (getIntent().getBooleanExtra(SHOW_LAUNCHER, false)) return new String[]{"--launcher", rom};
        return new String[]{rom};
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK &&
                !event.isFromSource(InputDevice.SOURCE_GAMEPAD)) {
            if (event.getAction() == KeyEvent.ACTION_UP && !event.isCanceled()) onBackPressed();
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    @Override public void onBackPressed() {
        if (controls == null) {
            super.onBackPressed();
            return;
        }
        sendShortcut(gameplayControlsVisible ? KeyEvent.KEYCODE_MENU : KeyEvent.KEYCODE_ESCAPE);
    }

    private void sendShortcut(int key) {
        onNativeKeyDown(key);
        onNativeKeyUp(key);
    }

    // Called by the SDL thread at the host's launcher and gameplay boundaries.
    public void setGameplayControlsVisible(boolean visible) {
        runOnUiThread(() -> {
            if (controls == null || isFinishing()) return;
            gameplayControlsVisible = visible;
            updateControls();
        });
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
        addMenuButton();
        inputs = getSystemService(InputManager.class);
        updateControls();
    }

    private void addMenuButton() {
        menuOverlay = new FrameLayout(this);
        menuOverlay.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            } else {
                view.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        float density = getResources().getDisplayMetrics().density;
        Button menu = new Button(this);
        menu.setText("Menu");
        menu.setFocusable(false);
        menu.setAlpha(.85f);
        menu.setOnClickListener(v -> sendShortcut(KeyEvent.KEYCODE_MENU));
        FrameLayout.LayoutParams position = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, Math.round(48 * density), Gravity.TOP | Gravity.END);
        int margin = Math.round(8 * density);
        position.setMargins(margin, margin, margin, 0);
        menuOverlay.addView(menu, position);
        mLayout.addView(menuOverlay, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
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
        menuOverlay.setVisibility(gameplayControlsVisible ? View.VISIBLE : View.GONE);
        if (!gameplayControlsVisible) {
            controls.release();
            controls.setVisibility(View.GONE);
            return;
        }
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
