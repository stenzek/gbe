package com.example.user.gbe;

public class GBSystem {
	/* System modes */
    public static final int SYSTEM_MODE_DMG = 0;
    public static final int SYSTEM_MODE_SGB = 1;
	public static final int SYSTEM_MODE_CGB = 2;

    static {
        System.loadLibrary("gbe");
    }

	public GBSystem() throws GBSystemException {
		nativeInit();
	}

    public void close() {
        nativeDestroy();
    }

	/* Native data */
	private long nativePointer;

	/* Native methods */
    private native void nativeInit() throws GBSystemException;
    private native void nativeDestroy();
	public native void loadCartridge(byte[] cartData) throws GBSystemException;
	public native int getCartridgeMode() throws GBSystemException;
	public native String getCartridgeName() throws GBSystemException;
	public native void bootSystem(int systemMode) throws GBSystemException;
	public native boolean isPaused();
	public native void setPaused(boolean paused);
	public native double executeFrame();

	/* Native callbacks */
	private void onPresentDisplayBuffer(byte[] pixels, int rowPitch) {
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
}
