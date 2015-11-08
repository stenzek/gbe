package com.example.user.gbe;

public class GBSystem {
	/* System modes */
    public static final int SYSTEM_MODE_DMG = 0;
    public static final int SYSTEM_MODE_SGB = 1;
	public static final int SYSTEM_MODE_CGB = 2;

	public GBSystem() throws GBSystemException {
		nativeInit();
	}

	@Override
	protected void finalize() {
		nativeDestroy();
	}

	/* Native data */
	private long nativePointer;

	/* Native methods */
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
	private byte[] onLoadCartridgeRAM(int expectedDataSize) {
	}
	private void onSaveCartridgeRAM(byte[] data, int dataSize) {
	}
	private byte[] onLoadCartridgeRTC(int expectedDataSize) {
	}
	private void onSaveCartridgeRTC(byte[] data, int dataSize) {
	}	
}
