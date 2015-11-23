package com.example.user.gbe;

import android.app.Activity;
import android.content.Intent;
import android.opengl.GLSurfaceView;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Button;
import android.widget.PopupMenu;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * An example full-screen activity that shows and hides the system UI (i.e.
 * status bar and navigation/system bar) with user interaction.
 */
public class GameActivity extends Activity {
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

    private GLSurfaceView mGLSurfaceView;
    private boolean mToolbarVisible;
    private GBSystem gbSystem;
    private SaveStateManager mSaveStateManager;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_game);

        mToolbarVisible = true;
        mGLSurfaceView = (GLSurfaceView)findViewById(R.id.gbDisplayView);

        // Set up the user interaction to manually showToolbar or hideToolbar the system UI.
        /*mGLSurfaceView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                // re-hideToolbar the controls
                if (mToolbarVisible)
                    hideToolbar();
            }
        });*/

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
        findViewById(R.id.button_pad_start).setOnTouchListener(mPadTouchListener);

        // Hook up menu
        final Button menuButton = (Button)findViewById(R.id.menu);
        menuButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                PopupMenu popupMenu = new PopupMenu(GameActivity.this, menuButton);
                popupMenu.getMenuInflater().inflate(R.menu.menu_game, popupMenu.getMenu());
                popupMenu.setOnMenuItemClickListener(mPopupMenuClickListener);
                popupMenu.setOnDismissListener(mPopupMenuDismissListener);
                popupMenu.show();
            }
        });
    }

    @Override
    protected void onPostCreate(Bundle savedInstanceState) {
        super.onPostCreate(savedInstanceState);
        hideSystemUI();
    }

    @Override
    public void onBackPressed() {
        // Restore system UI.
        showSystemUI();

        // End emulation.
        endEmulation();
    }

    @Override
    public void onPause() {
        if (gbSystem != null && gbSystem.isRunning())
            gbSystem.pause();

        // Restore the system UI.
        showSystemUI();
        super.onPause();
    }

    @Override
    public void onResume() {
        super.onResume();

        assert(gbSystem != null);
        if (!gbSystem.isRunning())
            gbSystem.resume();

        // Hide the system UI.
        hideSystemUI();
    }

    private void hideSystemUI() {
        // Hide the system UI.
        mGLSurfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
    }

    private void showSystemUI() {
        // Restore the system UI.
        mGLSurfaceView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
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

        // Search for an auto save.
        SaveStateManager.SaveState autoSaveState = SaveStateManager.getAutoSave(this, romPath);
        if (autoSaveState != null) {
            Log.d("loadRomAndBoot", "Loading auto save state from " + autoSaveState.getDate().toString());
            Toast.makeText(GameActivity.this, "Attempting to resume from save at " + autoSaveState.getDate().toString(), Toast.LENGTH_SHORT).show();
        }

        // Boot system with rom
        try {
            gbSystem = new GBSystem(mGLSurfaceView);
            gbSystem.start(cartData);

            // Load state if one exists.
            if (autoSaveState != null)
                gbSystem.loadState(autoSaveState.getData());
        } catch (GBSystemException e) {
            e.printStackTrace();
            Toast.makeText(GameActivity.this, "Booting failed: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            endEmulation();
            return;
        }

        // Create save state manager
        mSaveStateManager = new SaveStateManager(this, romPath);
    }

    public void endEmulation() {
        Log.i("GameActivity", "Ending emulation.");
        if (gbSystem != null) {
            // Save the state to the autostate save
            if (mSaveStateManager != null) {
                if (mSaveStateManager.createAutoSave(gbSystem))
                    Toast.makeText(GameActivity.this, "Saved", Toast.LENGTH_SHORT).show();
                else
                    Toast.makeText(GameActivity.this, "Automatic save failed.", Toast.LENGTH_SHORT).show();
            }
            gbSystem.close();
            gbSystem = null;
        }

        finish();
    }

    private PopupMenu.OnMenuItemClickListener mPopupMenuClickListener = new PopupMenu.OnMenuItemClickListener() {
        @Override
        public boolean onMenuItemClick(MenuItem item) {
            int id = item.getItemId();
            switch (id)
            {
                // Save State
                case R.id.save_state: {
                    if (mSaveStateManager.createManualSave(gbSystem)) {
                        Toast.makeText(GameActivity.this, "Save created.", Toast.LENGTH_SHORT).show();
                    } else {
                        Toast.makeText(GameActivity.this, "Save failed.", Toast.LENGTH_SHORT).show();
                    }
                    return true;
                }

                // Load State
                case R.id.load_state: {
                    try {
                        SaveStateManager.SaveState saveState = mSaveStateManager.getLatestManualSave();
                        if (saveState != null) {
                            gbSystem.loadState(saveState.getData());
                            Toast.makeText(GameActivity.this, "Save loaded.", Toast.LENGTH_SHORT).show();
                        } else {
                            Toast.makeText(GameActivity.this, "No save found.", Toast.LENGTH_SHORT).show();
                        }
                    } catch (GBSystemException e) {
                        e.printStackTrace();
                        Toast.makeText(GameActivity.this, "Load failed.", Toast.LENGTH_SHORT).show();
                    }
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
                    startActivity(new Intent(GameActivity.this, SettingsActivity.class));
                    return true;
                }

                // Exit
                case R.id.exit: {
                    endEmulation();
                    return true;
                }
            }

            return false;
        }
    };
    private PopupMenu.OnDismissListener mPopupMenuDismissListener = new PopupMenu.OnDismissListener() {
        @Override
        public void onDismiss(PopupMenu menu) {
            // Re-hide the system UI after clicking the button shows it.
            hideSystemUI();
        }
    };

    private View.OnTouchListener mPadTouchListener = new View.OnTouchListener() {
        @Override
        public boolean onTouch(View view, MotionEvent motionEvent) {
            if (motionEvent.getAction() != MotionEvent.ACTION_UP && motionEvent.getAction() != MotionEvent.ACTION_DOWN)
                return false;

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
