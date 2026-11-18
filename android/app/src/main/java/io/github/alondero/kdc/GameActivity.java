package io.github.alondero.kdc;

import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import java.io.File;
import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
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

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void button(LinearLayout row, String label, int key) {
        Button button = new Button(this);
        button.setText(label);
        button.setContentDescription(label);
        button.setTextSize(13);
        button.setPadding(0, 0, 0, 0);
        button.setAlpha(.7f);
        button.setFocusable(false);
        button.setOnTouchListener((view, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    onNativeKeyDown(key);
                    view.setPressed(true);
                    break;
                case MotionEvent.ACTION_CANCEL:
                case MotionEvent.ACTION_UP:
                    onNativeKeyUp(key);
                    view.setPressed(false);
                    break;
            }
            return true;
        });
        row.addView(button, dp(label.length() > 3 ? 60 : 48), dp(46));
    }

    private LinearLayout column(int side) {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER);
        RelativeLayout.LayoutParams position = new RelativeLayout.LayoutParams(-2, -2);
        position.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
        position.addRule(side);
        mLayout.addView(column, position);
        return column;
    }

    private LinearLayout row(LinearLayout column) {
        LinearLayout row = new LinearLayout(this);
        column.addView(row);
        return row;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        if (mLayout == null) return;
        LinearLayout left = column(RelativeLayout.ALIGN_PARENT_LEFT);
        LinearLayout line = row(left);
        button(line, "L", KeyEvent.KEYCODE_C);
        button(line, "Select", KeyEvent.KEYCODE_SHIFT_RIGHT);
        button(row(left), "↑", KeyEvent.KEYCODE_DPAD_UP);
        line = row(left);
        button(line, "←", KeyEvent.KEYCODE_DPAD_LEFT);
        button(line, "↓", KeyEvent.KEYCODE_DPAD_DOWN);
        button(line, "→", KeyEvent.KEYCODE_DPAD_RIGHT);

        LinearLayout right = column(RelativeLayout.ALIGN_PARENT_RIGHT);
        line = row(right);
        button(line, "Start", KeyEvent.KEYCODE_ENTER);
        button(line, "R", KeyEvent.KEYCODE_V);
        button(row(right), "X", KeyEvent.KEYCODE_S);
        line = row(right);
        button(line, "Y", KeyEvent.KEYCODE_A);
        button(line, "B", KeyEvent.KEYCODE_Z);
        button(line, "A", KeyEvent.KEYCODE_X);
    }
}
