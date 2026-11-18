package io.github.alondero.kdc;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.CornerPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.DisplayCutout;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;

import org.libsdl.app.SDLActivity;

final class TouchController extends ViewGroup {
    private enum Key {
        UP("\u25b2", KeyEvent.KEYCODE_DPAD_UP), DOWN("\u25bc", KeyEvent.KEYCODE_DPAD_DOWN),
        LEFT("\u25c0", KeyEvent.KEYCODE_DPAD_LEFT), RIGHT("\u25b6", KeyEvent.KEYCODE_DPAD_RIGHT),
        X("X", KeyEvent.KEYCODE_S), B("B", KeyEvent.KEYCODE_Z),
        Y("Y", KeyEvent.KEYCODE_A), A("A", KeyEvent.KEYCODE_X),
        L("L", KeyEvent.KEYCODE_C), R("R", KeyEvent.KEYCODE_V),
        SELECT("SELECT", KeyEvent.KEYCODE_SHIFT_RIGHT), START("START", KeyEvent.KEYCODE_ENTER);

        final String label;
        final int code;
        final int bit = 1 << ordinal();
        Key(String label, int code) { this.label = label; this.code = code; }
    }

    private static final Key[] KEYS = Key.values();
    private final float density;
    private final Path cross = new Path();
    private final Paint crossPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private float unit, padX, padY;
    private int held;
    private boolean tracking;

    TouchController(Context context) {
        super(context);
        density = getResources().getDisplayMetrics().density;
        setWillNotDraw(false);
        crossPaint.setPathEffect(new CornerPathEffect(5 * density));
        for (Key key : KEYS) {
            Button button = new Button(context);
            button.setText(key.label);
            button.setContentDescription(key.name());
            button.setTextColor(0xffffffff);
            button.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
            button.setGravity(Gravity.CENTER);
            button.setPadding(0, 0, 0, 0);
            button.setStateListAnimator(null);
            button.setFocusable(false);
            boolean face = key == Key.A || key == Key.B || key == Key.X || key == Key.Y;
            boolean direction = key.ordinal() <= Key.RIGHT.ordinal();
            int color = key == Key.X || key == Key.Y ? 0xce78719e :
                    face ? 0xd05b4489 : 0xb42e303d;
            float radius = direction ? 6 * density : 100 * density;
            StateListDrawable background = new StateListDrawable();
            background.addState(new int[]{android.R.attr.state_pressed}, shape(0xee9c83b7, radius));
            if (!direction) background.addState(new int[]{}, shape(color, radius));
            button.setBackground(background);
            button.setOnClickListener(v -> {
                // Accessibility click. Direct touches are handled together below.
                if ((held & key.bit) == 0) {
                    SDLActivity.onNativeKeyDown(key.code);
                    SDLActivity.onNativeKeyUp(key.code);
                }
            });
            addView(button);
        }
    }

    private GradientDrawable shape(int color, float radius) {
        GradientDrawable shape = new GradientDrawable();
        shape.setColor(color);
        shape.setCornerRadius(radius);
        shape.setStroke(Math.max(1, Math.round(density)), 0xbbe3dcf1);
        return shape;
    }

    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        DisplayCutout cutout = insets.getDisplayCutout();
        setPadding(cutout == null ? 0 : cutout.getSafeInsetLeft(), 0,
                cutout == null ? 0 : cutout.getSafeInsetRight(),
                cutout == null ? 0 : cutout.getSafeInsetBottom());
        return super.onApplyWindowInsets(insets);
    }

    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int width = MeasureSpec.getSize(widthSpec), height = MeasureSpec.getSize(heightSpec);
        setMeasuredDimension(width, height);
        float usableWidth = width - getPaddingLeft() - getPaddingRight();
        unit = Math.min(48 * density, Math.min(usableWidth / 11, (height - getPaddingBottom()) / 5.8f));
        for (Key key : KEYS) {
            boolean menu = key == Key.SELECT || key == Key.START;
            boolean shoulder = key == Key.L || key == Key.R;
            Button button = button(key);
            button.setTextSize(TypedValue.COMPLEX_UNIT_PX, unit * (menu ? .25f : .39f));
            int w = Math.round(unit * (menu ? 1.45f : shoulder ? 1.85f : 1));
            int h = Math.round(unit * (menu ? .60f : shoulder ? .72f : 1));
            button.measure(MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY),
                    MeasureSpec.makeMeasureSpec(h, MeasureSpec.EXACTLY));
        }
    }

    private Button button(Key key) { return (Button)getChildAt(key.ordinal()); }

    private void place(Key key, float x, float y) {
        Button b = button(key);
        int left = Math.round(x - b.getMeasuredWidth() / 2f);
        int top = Math.round(y - b.getMeasuredHeight() / 2f);
        b.layout(left, top, left + b.getMeasuredWidth(), top + b.getMeasuredHeight());
    }

    @Override protected void onLayout(boolean changed, int l, int t, int r, int b) {
        if (changed) release();
        float left = getPaddingLeft(), right = getWidth() - getPaddingRight();
        float bottom = getHeight() - getPaddingBottom();
        padX = left + 2.05f * unit;
        padY = bottom - 1.95f * unit;
        float faceX = right - 2.05f * unit;
        float step = .81f * unit;
        float arm = .5f * unit, tip = step + arm;
        cross.rewind();
        cross.moveTo(-arm, -tip);
        cross.lineTo(arm, -tip);
        cross.lineTo(arm, -arm);
        cross.lineTo(tip, -arm);
        cross.lineTo(tip, arm);
        cross.lineTo(arm, arm);
        cross.lineTo(arm, tip);
        cross.lineTo(-arm, tip);
        cross.lineTo(-arm, arm);
        cross.lineTo(-tip, arm);
        cross.lineTo(-tip, -arm);
        cross.lineTo(-arm, -arm);
        cross.close();
        place(Key.UP, padX, padY - step);
        place(Key.DOWN, padX, padY + step);
        place(Key.LEFT, padX - step, padY);
        place(Key.RIGHT, padX + step, padY);
        place(Key.X, faceX, padY - step);
        place(Key.B, faceX, padY + step);
        place(Key.Y, faceX - step, padY);
        place(Key.A, faceX + step, padY);
        place(Key.L, padX, padY - 2.25f * unit);
        place(Key.R, faceX, padY - 2.25f * unit);
        float middle = (left + right) / 2;
        place(Key.SELECT, middle - .86f * unit, bottom - .72f * unit);
        place(Key.START, middle + .86f * unit, bottom - .72f * unit);
    }

    @Override protected void onDraw(Canvas canvas) {
        canvas.save();
        canvas.translate(padX, padY);
        crossPaint.setStyle(Paint.Style.FILL);
        crossPaint.setColor(0xbd2e303d);
        canvas.drawPath(cross, crossPaint);
        crossPaint.setStyle(Paint.Style.STROKE);
        crossPaint.setStrokeWidth(density);
        crossPaint.setColor(0xbbe3dcf1);
        canvas.drawPath(cross, crossPaint);
        canvas.restore();
    }

    private int hit(float x, float y) {
        float dx = x - padX, dy = y - padY;
        float ax = Math.abs(dx), ay = Math.abs(dy);
        if (Math.max(ax, ay) <= 1.48f * unit) {
            if (Math.max(ax, ay) < .22f * unit) return 0;
            // Half-angle of a 45 degree sector. Corners press both directions.
            int keys = 0;
            if (ax >= .4142f * ay) keys |= dx < 0 ? Key.LEFT.bit : Key.RIGHT.bit;
            if (ay >= .4142f * ax) keys |= dy < 0 ? Key.UP.bit : Key.DOWN.bit;
            return keys;
        }
        Key closest = null;
        float distance = Float.MAX_VALUE;
        for (Key key : KEYS) {
            if (key.ordinal() <= Key.RIGHT.ordinal()) continue;
            Button b = button(key);
            dx = x - (b.getLeft() + b.getWidth() / 2f);
            dy = y - (b.getTop() + b.getHeight() / 2f);
            // Extend the short visible pills to a full finger-height target.
            if (Math.abs(dx) > b.getWidth() / 2f + .05f * unit ||
                    Math.abs(dy) > .55f * unit) continue;
            float d = dx * dx + dy * dy;
            if (d < distance) { distance = d; closest = key; }
        }
        return closest == null ? 0 : closest.bit;
    }

    private void apply(int next) {
        int changed = held ^ next;
        for (Key key : KEYS) {
            if ((changed & key.bit) == 0) continue;
            boolean down = (next & key.bit) != 0;
            button(key).setPressed(down);
            if (down) SDLActivity.onNativeKeyDown(key.code);
            else SDLActivity.onNativeKeyUp(key.code);
        }
        held = next;
    }

    void release() {
        tracking = false;
        apply(0);
    }

    @Override public boolean dispatchTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN) {
            float x = event.getX(), y = event.getY();
            tracking = hit(x, y) != 0 || (Math.abs(x - padX) <= 1.48f * unit &&
                    Math.abs(y - padY) <= 1.48f * unit);
        }
        if (!tracking) return false;
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            release();
            return true;
        }
        int next = 0;
        for (int i = 0; i < event.getPointerCount(); i++) {
            if (action == MotionEvent.ACTION_POINTER_UP && i == event.getActionIndex()) continue;
            next |= hit(event.getX(i), event.getY(i));
        }
        apply(next);
        return true;
    }
}
