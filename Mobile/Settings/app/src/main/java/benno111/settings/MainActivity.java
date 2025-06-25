package benno111.settings;

import android.content.Intent;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Toast;
import android.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import java.io.DataOutputStream;

public class MainActivity extends AppCompatActivity {

    private boolean isRootGranted = false;
    private static final String ROOT_PASSWORD = "1234";

    private boolean requestRootAccess() {
        try {
            Process su = Runtime.getRuntime().exec("su");
            DataOutputStream output = new DataOutputStream(su.getOutputStream());
            output.writeBytes("exit\n");
            output.flush();
            return su.waitFor() == 0;
        } catch (Exception e) {
            return false;
        }
    }

    private void toggleAirplaneMode() {
        try {
            Process su = Runtime.getRuntime().exec("su");
            DataOutputStream output = new DataOutputStream(su.getOutputStream());
            output.writeBytes("settings put global airplane_mode_on 1\n");
            output.writeBytes("am broadcast -a android.intent.action.AIRPLANE_MODE --ez state true\n");
            output.writeBytes("exit\n");
            output.flush();
            su.waitFor();
            Toast.makeText(this, "Airplane mode enabled via root", Toast.LENGTH_SHORT).show();
        } catch (Exception e) {
            Toast.makeText(this, "Failed to toggle airplane mode", Toast.LENGTH_SHORT).show();
        }
    }

    private void showRootPasswordPrompt(Button rootButton, Button airplaneButton) {
        EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);

        new AlertDialog.Builder(this)
            .setTitle("Root Login")
            .setMessage("Enter root password:")
            .setView(input)
            .setPositiveButton("OK", (dialog, which) -> {
                String entered = input.getText().toString();
                if (entered.equals(ROOT_PASSWORD)) {
                    if (requestRootAccess()) {
                        isRootGranted = true;
                        Toast.makeText(this, "✅ Root access granted", Toast.LENGTH_SHORT).show();
                        rootButton.setText("Root: Logged In");
                        airplaneButton.setEnabled(true);
                    } else {
                        Toast.makeText(this, "❌ Root access denied", Toast.LENGTH_SHORT).show();
                    }
                } else {
                    Toast.makeText(this, "Incorrect password", Toast.LENGTH_SHORT).show();
                }
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void openSettings(String action) {
        Intent intent = new Intent(action);
        if (intent.resolveActivity(getPackageManager()) != null) {
            startActivity(intent);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        Button airplaneBtn = findViewById(R.id.btnAirplane);
        Button rootLoginBtn = findViewById(R.id.btnRootLogin);

        airplaneBtn.setOnClickListener(v -> {
            if (isRootGranted) toggleAirplaneMode();
            else Toast.makeText(this, "Login to root first", Toast.LENGTH_SHORT).show();
        });

        rootLoginBtn.setOnClickListener(v -> {
            showRootPasswordPrompt(rootLoginBtn, airplaneBtn);
        });

        findViewById(R.id.btnWifi).setOnClickListener(v -> openSettings(Settings.ACTION_WIFI_SETTINGS));
        findViewById(R.id.btnBluetooth).setOnClickListener(v -> openSettings(Settings.ACTION_BLUETOOTH_SETTINGS));
        findViewById(R.id.btnDisplay).setOnClickListener(v -> openSettings(Settings.ACTION_DISPLAY_SETTINGS));
        findViewById(R.id.btnSound).setOnClickListener(v -> openSettings(Settings.ACTION_SOUND_SETTINGS));
        findViewById(R.id.btnBattery).setOnClickListener(v -> openSettings(Settings.ACTION_BATTERY_SAVER_SETTINGS));
        findViewById(R.id.btnApps).setOnClickListener(v -> openSettings(Settings.ACTION_APPLICATION_SETTINGS));
        findViewById(R.id.btnAccessibility).setOnClickListener(v -> openSettings(Settings.ACTION_ACCESSIBILITY_SETTINGS));
        findViewById(R.id.btnAbout).setOnClickListener(v -> openSettings(Settings.ACTION_DEVICE_INFO_SETTINGS));
    }
}
