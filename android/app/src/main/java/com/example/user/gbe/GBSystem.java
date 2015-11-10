package com.example.user.gbe;

import android.graphics.Bitmap;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

public class GBSystem {
	/* System modes */
    public static final int SYSTEM_MODE_DMG = 0;
    public static final int SYSTEM_MODE_SGB = 1;
	public static final int SYSTEM_MODE_CGB = 2;
	public static final int SCREEN_WIDTH = 160;
	public static final int SCREEN_HEIGHT = 144;
	public static final int PAD_DIRECTION_RIGHT = 0x01;
	public static final int PAD_DIRECTION_LEFT = 0x02;
	public static final int PAD_DIRECTION_UP = 0x04;
	public static final int PAD_DIRECTION_DOWN = 0x08;
	public static final int PAD_BUTTON_A = 0x01;
	public static final int PAD_BUTTON_B = 0x02;
	public static final int PAD_BUTTON_SELECT = 0x04;
	public static final int PAD_BUTTON_START = 0x08;

    static {
        System.loadLibrary("gbe");
    }

	public GBSystem(GBDisplayView displayView) throws GBSystemException {
		init(displayView);
		nativeInit();
	}

    public void close() {
		if (workerThreadRunning)
			stopWorkerThread();

        nativeDestroy();
    }

	/* Native data */
	private long nativePointer;

	/* Native methods */
    private native void nativeInit() throws GBSystemException;
    private native void nativeDestroy();
	private native void nativeLoadCartridge(byte[] cartData) throws GBSystemException;
	private native int nativeGetCartridgeMode() throws GBSystemException;
	private native String nativeGetCartridgeName() throws GBSystemException;
	private native void nativeBootSystem(int systemMode) throws GBSystemException;
	private native double nativeExecuteFrame();
	private native void nativeCopyScreenBuffer(Bitmap destinationBitmap);
	private native void nativeSetPaused(boolean paused);
	private native void nativeSetPadDirectionState(int state);
	private native void nativeSetPadButtonState(int state);

	/* Native callbacks */
	private void onScreenBufferReady() {
		synchronized (this) {
			nativeCopyScreenBuffer(currentPendingBuffer);
			if (!presentPending) {
				presentPending = true;
				displayView.post(new Runnable() {
					public void run() {
						synchronized (GBSystem.this) {
							Bitmap toPresent = currentPendingBuffer;
							displayView.setDisplayBitmap(toPresent);
							currentPendingBuffer = currentPresentingBuffer;
							currentPresentingBuffer = toPresent;
							presentPending = false;
						}
					}
				});
			}
		}
	}
	private boolean onLoadCartridgeRAM(byte[] data, int expectedDataSize) {
		return false;
	}
	private void onSaveCartridgeRAM(byte[] data, int dataSize) {
	}
	private boolean onLoadCartridgeRTC(byte[] data, int expectedDataSize) {
        return false;
	}
	private void onSaveCartridgeRTC(byte[] data, int dataSize) {
	}

	/* Java Data */
	private Bitmap currentPresentingBuffer = null;
	private Bitmap currentPendingBuffer = null;
	private boolean presentPending = false;
	private GBDisplayView displayView = null;
	private Thread workerThread = null;
	private volatile boolean workerThreadRunning = false;

	private void init(GBDisplayView displayView) {
		currentPresentingBuffer = Bitmap.createBitmap(SCREEN_WIDTH, SCREEN_HEIGHT, Bitmap.Config.ARGB_8888);
		currentPendingBuffer = Bitmap.createBitmap(SCREEN_WIDTH, SCREEN_HEIGHT, Bitmap.Config.ARGB_8888);
		this.displayView = displayView;

		// hook buttons
	}

	private void startWorkerThread() {
		assert(workerThread == null);
		workerThreadRunning = true;
		workerThread = new Thread(new Runnable() {
			public void run() {
				workerThreadEntryPoint();
			}
		});
		workerThread.start();
	}

	private void stopWorkerThread() {
		assert(workerThread != null);
		workerThreadRunning = false;
		try {
			workerThread.join();
		} catch (InterruptedException e) {

		}
		workerThread = null;
	}

	public void start(byte[] cartData) throws GBSystemException {
		int systemBootMode = GBSystem.SYSTEM_MODE_DMG;
		if (cartData != null) {
			Log.d("GBSystem", "Parsing cartridge");
			nativeLoadCartridge(cartData);

			// Switch modes according to cart inserted.
			int cartridgeMode = nativeGetCartridgeMode();
			Log.i("GBSystem", String.format("Cartridge name: '%s' (mode %d)", nativeGetCartridgeName(), cartridgeMode));
			systemBootMode = cartridgeMode;
		}

		Log.i("GBSystem", "Booting system mode: " + systemBootMode);
		nativeBootSystem(systemBootMode);

		Log.i("GBSystem", "Starting worker thread.");
		startWorkerThread();
	}

	public void pause() {
		stopWorkerThread();
		nativeSetPaused(true);
		Log.i("GBSystem", "Execution paused.");
	}

	public void resume() {
		nativeSetPaused(false);
		startWorkerThread();
		Log.i("GBSystem", "Execution resumed.");
	}

	public boolean isRunning() {
		return workerThreadRunning;
	}

	private void workerThreadEntryPoint() {
		Log.d("WorkerThread", "Starting");

		int lastPadDirectionState = mPadDirectionState;
		int lastPadButtonState = mPadButtonState;
		while (workerThreadRunning)
		{
			if (lastPadDirectionState != mPadDirectionState) {
				nativeSetPadDirectionState(mPadDirectionState);
				lastPadDirectionState = mPadDirectionState;
			}
			if (lastPadButtonState != mPadButtonState) {
				nativeSetPadButtonState(mPadButtonState);
				lastPadButtonState = mPadButtonState;
			}

			double sleepTime = nativeExecuteFrame();
			if (sleepTime > 0.001) {
				int millis = (int) Math.floor(sleepTime * 1000);
				//Log.d("GBSystem", String.format("sleepTime: %f, millis: %d", sleepTime, millis));
				try {
					Thread.sleep(millis);
				}
				catch (InterruptedException e) {

				}
			}
		}
		Log.d("WorkerThread", "Exiting");
	}

	private volatile int mPadDirectionState = 0;
	private volatile int mPadButtonState = 0;

	public void setPadDirection(int direction, boolean down) {
		Log.d("GBSystem", String.format("setPadDirection(%d, %s)", direction, down ? "true" : "false"));
		mPadDirectionState = (down) ? (mPadDirectionState | direction) : (mPadDirectionState & ~direction);
	}
	public void setPadButton(int button, boolean down) {
		Log.d("GBSystem", String.format("setPadButton(%d, %s)", button, down ? "true" : "false"));
		mPadButtonState = (down) ? (mPadButtonState | button) : (mPadButtonState & ~button);
	}

	/*private View.OnTouchListener mTouchListener = new View.OnTouchListener() {
		@Override
		public boolean onTouch(View view, MotionEvent motionEvent) {
			Log.d("ontouch", "ontouch");
			boolean isDown = (motionEvent.getAction() == MotionEvent.ACTION_DOWN);
			switch (view.getId()) {
				case R.id.button_pad_left:
					mPadDirectionState = (isDown) ? (mPadDirectionState | PAD_DIRECTION_LEFT) : (mPadDirectionState & ~PAD_DIRECTION_LEFT);
					return true;
				case R.id.button_pad_right:
					mPadDirectionState = (isDown) ? (mPadDirectionState | PAD_DIRECTION_RIGHT) : (mPadDirectionState & ~PAD_DIRECTION_RIGHT);
					return true;
				case R.id.button_pad_up:
					mPadDirectionState = (isDown) ? (mPadDirectionState | PAD_DIRECTION_UP) : (mPadDirectionState & ~PAD_DIRECTION_UP);
					return true;
				case R.id.button_pad_down:
					mPadDirectionState = (isDown) ? (mPadDirectionState | PAD_DIRECTION_DOWN) : (mPadDirectionState & ~PAD_DIRECTION_DOWN);
					return true;
				case R.id.button_pad_a:
					mPadButtonState = (isDown) ? (mPadButtonState | PAD_BUTTON_A) : (mPadButtonState & ~PAD_BUTTON_A);
					return true;
				case R.id.button_pad_b:
					mPadButtonState = (isDown) ? (mPadButtonState | PAD_BUTTON_B) : (mPadButtonState & ~PAD_BUTTON_B);
					return true;
				case R.id.button_pad_start:
					mPadButtonState = (isDown) ? (mPadButtonState | PAD_BUTTON_START) : (mPadButtonState & ~PAD_BUTTON_START);
					return true;
				case R.id.button_select:
					mPadButtonState = (isDown) ? (mPadButtonState | PAD_BUTTON_SELECT) : (mPadButtonState & ~PAD_BUTTON_SELECT);
					return true;
			}
			return false;
		}
	}*/
}
