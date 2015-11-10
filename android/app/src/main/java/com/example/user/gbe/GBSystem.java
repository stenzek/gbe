package com.example.user.gbe;

import android.graphics.Bitmap;
import android.util.Log;
import android.widget.ImageView;

public class GBSystem {
	/* System modes */
    public static final int SYSTEM_MODE_DMG = 0;
    public static final int SYSTEM_MODE_SGB = 1;
	public static final int SYSTEM_MODE_CGB = 2;
	public static final int SCREEN_WIDTH = 160;
	public static final int SCREEN_HEIGHT = 144;

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
		while (workerThreadRunning)
		{
			double sleepTime = nativeExecuteFrame();
			if (sleepTime > 0.01) {
				int millis = (int) Math.floor(sleepTime * 1000);
				try {
					Thread.sleep(millis);
				}
				catch (InterruptedException e) {

				}
			}
		}
		Log.d("WorkerThread", "Exiting");
	}
}
