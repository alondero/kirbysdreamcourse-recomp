package io.github.alondero.kdc;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.*;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;

public class ImportActivity extends Activity {
    static final String ROM = "course.sfc";
    private static final String SHA256 = "0f984dc5fe8293f75e3b8fad98b0cb564706d9b1e3902b56415aa399c2d4df2b";
    private TextView message;
    private Button pick, play;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        int margin = (int)(24 * getResources().getDisplayMetrics().density);
        box.setPadding(margin, margin, margin, margin);
        message = new TextView(this);
        message.setText("Choose your headerless Kirby’s Dream Course (USA) ROM to play.");
        box.addView(message);
        pick = new Button(this);
        pick.setText("Choose ROM");
        pick.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE).setType("*/*"), 1));
        box.addView(pick);
        play = new Button(this);
        play.setText("Play");
        play.setEnabled(new File(getFilesDir(), ROM).isFile());
        play.setOnClickListener(v -> prepareGame(null));
        box.addView(play);
        setContentView(box);
    }

    // The launcher UI reads resources through stdio in SDL's internal storage.
    private void installAssets(String path) throws IOException {
        String[] children = getAssets().list(path);
        File output = new File(getFilesDir(), path);
        if (children != null && children.length > 0) {
            Files.createDirectories(output.toPath());
            for (String child : children) installAssets(path + "/" + child);
        } else {
            try (InputStream input = getAssets().open(path)) {
                Files.copy(input, output.toPath(), StandardCopyOption.REPLACE_EXISTING);
            }
        }
    }

    private void prepareGame(Uri uri) {
        pick.setEnabled(false);
        play.setEnabled(false);
        message.setText(uri == null ? "Preparing game?" : "Checking ROM?");
        new Thread(() -> {
            try {
                if (uri != null) importRom(uri);
                installAssets("assets");
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    resetControls("ROM ready.");
                    startActivity(new Intent(this, GameActivity.class));
                });
            } catch (Exception e) {
                runOnUiThread(() -> resetControls("Could not open game: " + e.getMessage()));
            }
        }, "Game preparation").start();
    }

    private void resetControls(String status) {
        message.setText(status);
        pick.setEnabled(true);
        play.setEnabled(new File(getFilesDir(), ROM).isFile());
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != 1 || result != RESULT_OK || data == null || data.getData() == null) return;
        prepareGame(data.getData());
    }

    private void importRom(Uri uri) throws IOException, java.security.NoSuchAlgorithmException {
        File temp = null;
        try {
            temp = File.createTempFile("import-", ".tmp", getFilesDir());
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (InputStream input = getContentResolver().openInputStream(uri);
                 OutputStream output = new FileOutputStream(temp)) {
                if (input == null) throw new IOException("File unavailable");
                byte[] buffer = new byte[16384];
                int total = 0, count;
                while ((count = input.read(buffer)) != -1) {
                    total += count;
                    if (total > 1048576) throw new IOException("Expected a headerless 1 MiB USA ROM");
                    digest.update(buffer, 0, count);
                    output.write(buffer, 0, count);
                }
            }
            StringBuilder hash = new StringBuilder();
            for (byte b : digest.digest()) hash.append(String.format("%02x", b & 255));
            if (!SHA256.contentEquals(hash)) throw new IOException("This file does not match the supported USA ROM");
            Files.move(temp.toPath(), new File(getFilesDir(), ROM).toPath(),
                       StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);

        } finally {
            if (temp != null) temp.delete();
        }
    }
}
