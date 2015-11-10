package com.example.user.gbe;

import android.content.Intent;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * An example full-screen activity that shows and hides the system UI (i.e.
 * status bar and navigation/system bar) with user interaction.
 */
public class GameActivity extends AppCompatActivity {
    /**
     * Whether or not the system UI should be auto-hidden after
     * {@link #AUTO_HIDE_DELAY_MILLIS} milliseconds.
     */
    private static final boolean AUTO_HIDE = true;

    /**
     * If {@link #AUTO_HIDE} is set, the number of milliseconds to wait after
     * user interaction before hiding the system UI.
     */
    private static final int AUTO_HIDE_DELAY_MILLIS = 3000;

    /**
     * Some older devices needs a small delay between UI widget updates
     * and a change of the status and navigation bar.
     */
    private static final int UI_ANIMATION_DELAY = 300;

    private GBDisplayView mDisplayView;
    private boolean mToolbarVisible;
    private GBSystem gbSystem;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_game);

        mToolbarVisible = true;
        mDisplayView = (GBDisplayView)findViewById(R.id.gbDisplayView);

        // Set up the user interaction to manually showToolbar or hideToolbar the system UI.
        mDisplayView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                // re-hideToolbar the controls
                if (mToolbarVisible)
                    hideToolbar();
            }
        });

        // Pull parameters back from launcher.
        Intent intent = getIntent();
        String romPath = (intent != null) ? intent.getStringExtra("romPath") : null;
        if (intent == null || romPath == null) {
            // incomplete launch
            Toast.makeText(GameActivity.this, "Invalid launch parameters.", Toast.LENGTH_SHORT).show();
            finish();
            return;
        }

        // Load rom->boot
        loadRomAndBoot(romPath);

        // Hook pad buttons
        findViewById(R.id.button_pad_left).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_right).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_up).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_down).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_a).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_b).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_select).setOnTouchListener(mPadTouchListener);
        findViewById(R.id.button_pad_select).setOnTouchListener(mPadTouchListener);
    }

    @Override
    protected void onPostCreate(Bundle savedInstanceState) {
        super.onPostCreate(savedInstanceState);

        // Trigger the initial hideToolbar() shortly after the activity has been
        // created, to briefly hint to the user that UI controls
        // are available.
        //delayedHide(100);
        hideToolbar();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_game, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int id = item.getItemId();
        switch (id)
        {
            // Save State
            case R.id.save_state: {
                Toast.makeText(GameActivity.this, "Saving state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Load State
            case R.id.load_state: {
                Toast.makeText(GameActivity.this, "Loading state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Select Save State
            case R.id.select_save_state: {
                Toast.makeText(GameActivity.this, "Selecting save state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Enable Sound
            case R.id.enable_sound: {
                boolean soundEnabled = !item.isChecked();
                Toast.makeText(GameActivity.this, soundEnabled ? "Sound Enabled" : "Sound Disabled", Toast.LENGTH_SHORT).show();
                item.setChecked(soundEnabled);
                return true;
            }

            // Change speed
            case R.id.change_speed: {
                Toast.makeText(GameActivity.this, "Changing speed", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Settings
            case R.id.settings: {
                startActivity(new Intent(this, SettingsActivity.class));
                return true;
            }

            // Exit
            case R.id.exit: {
                endEmulation();
                return true;
            }
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onBackPressed() {
        // showToolbar menu if not visible, otherwise exit out
        if (!mToolbarVisible)
            showToolbar();
        else
            endEmulation();
    }

    @Override
    public void onPause() {
        if (gbSystem != null && gbSystem.isRunning())
            gbSystem.pause();

        super.onPause();
    }

    @Override
    public void onResume() {
        super.onResume();

        assert(gbSystem != null);
        if (!gbSystem.isRunning())
            gbSystem.resume();
    }

    private void hideToolbar() {
        // Hide UI first
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.hide();
        }
        mToolbarVisible = false;

        // Note that some of these constants are new as of API 16 (Jelly Bean)
        // and API 19 (KitKat). It is safe to use them, as they are inlined
        // at compile-time and do nothing on earlier devices.
        mDisplayView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
    }

    private void showToolbar() {
        // Show the system bar
        mDisplayView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);

        mToolbarVisible = true;

        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.show();
        }
    }

    private void loadRomAndBoot(String romPath)
    {
        // Read rom
        byte[] cartData = null;
        if (romPath != null) {
            try {
                Log.d("loadRomAndBoot", "Loading cartridge at " + romPath);

                File file = new File(romPath);
                FileInputStream inputStream = new FileInputStream(file);
                cartData = new byte[(int) file.length()];
                inputStream.read(cartData, 0, cartData.length);
                inputStream.close();
            } catch (FileNotFoundException e) {
                e.printStackTrace();
                Toast.makeText(GameActivity.this, "File open failed: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                endEmulation();
                return;
            } catch (IOException e) {
                e.printStackTrace();
                Toast.makeText(GameActivity.this, "File read failed: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                endEmulation();
                return;
            }
        }

        // Boot system with rom
        try {
            gbSystem = new GBSystem(mDisplayView);
            gbSystem.start(cartData);
        } catch (GBSystemException e) {
            e.printStackTrace();
            Toast.makeText(GameActivity.this, "Booting failed: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            endEmulation();
            return;
        }
    }

    public void endEmulation() {
        Log.i("GameActivity", "Ending emulation.");
        if (gbSystem != null) {
            gbSystem.close();
            gbSystem = null;
        }

        finish();
    }

    private View.OnTouchListener mPadTouchListener = new View.OnTouchListener() {
        @Override
        public boolean onTouch(View view, MotionEvent motionEvent) {
            boolean isDown = (motionEvent.getAction() == MotionEvent.ACTION_DOWN);
            switch (view.getId()) {
                case R.id.button_pad_left:
                    gbSystem.setPadDirection(GBSystem.PAD_DIRECTION_LEFT, isDown);
                    return true;
                case R.id.button_pad_right:
                    gbSystem.setPadDirection(GBSystem.PAD_DIRECTION_RIGHT, isDown);
                    return true;
                case R.id.button_pad_up:
                    gbSystem.setPadDirection(GBSystem.PAD_DIRECTION_UP, isDown);
                    return true;
                case R.id.button_pad_down:
                    gbSystem.setPadDirection(GBSystem.PAD_DIRECTION_DOWN, isDown);
                    return true;
                case R.id.button_pad_a:
                    gbSystem.setPadButton(GBSystem.PAD_BUTTON_A, isDown);
                    return true;
                case R.id.button_pad_b:
                    gbSystem.setPadButton(GBSystem.PAD_BUTTON_B, isDown);
                    return true;
                case R.id.button_pad_start:
                    gbSystem.setPadButton(GBSystem.PAD_BUTTON_START, isDown);
                    return true;
                case R.id.button_pad_select:
                    gbSystem.setPadButton(GBSystem.PAD_BUTTON_SELECT, isDown);
                    return true;
            }
            return false;
        }
    };
}
