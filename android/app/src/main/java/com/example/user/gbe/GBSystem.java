package com.example.user.gbe;

import android.graphics.Bitmap;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.opengl.Matrix;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

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

	public GBSystem(GLSurfaceView glSurfaceView) throws GBSystemException {
		init(glSurfaceView);
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
	private native void nativeCopyScreenBufferToBitmap(Bitmap destinationBitmap);
	private native void nativeCopyScreenBufferToTexture(int glTextureId);
	private native void nativeSetPaused(boolean paused);
	private native void nativeSetPadDirectionState(int state);
	private native void nativeSetPadButtonState(int state);
	private native byte[] nativeSaveState() throws GBSystemException;
    private native void nativeLoadState(byte[] data) throws GBSystemException;
	private native void nativeSetFrameLimiterEnabled(boolean enabled);

	/* Native callbacks */
	private void onScreenBufferReady() {
		if (mGBRenderer.mSurfaceReady) {
			//Log.d("GBSystem", "onScreenBufferReady -> queueing render");
			mGLSurfaceView.requestRender();
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

	/* Renderer */
	public class Renderer implements GLSurfaceView.Renderer {

		private int mSurfaceWidth;
		private int mSurfaceHeight;
		private volatile boolean mSurfaceReady = false;

		public Renderer() {
			mGLSurfaceView.setEGLContextClientVersion(2);
			mGLSurfaceView.setRenderer(this);
			mGLSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
		}

		@Override
		public void onSurfaceCreated(GL10 gl, EGLConfig config) {
			Log.d("GBRenderer", "onSurfaceCreated");
			compileShaders();
			createGLResources();

			synchronized (this) {
				mSurfaceWidth = mGLSurfaceView.getWidth();
				mSurfaceHeight = mGLSurfaceView.getHeight();
				mSurfaceReady = true;
			}
		}

		@Override
		public void onSurfaceChanged(GL10 gl, int width, int height) {
			Log.d("GBRenderer", String.format("onSurfaceCreated: %dx%d", width, height));
			synchronized (this) {
				mSurfaceWidth = width;
				mSurfaceHeight = height;
			}
		}

		@Override
		public void onDrawFrame(GL10 gl) {
			//Log.d("GBRenderer", "onDrawFrame");

			// Copy display buffer to GPU texture
			synchronized (this) {
				nativeCopyScreenBufferToTexture(mGLDisplayTexture);
			}

			// Clear screen
			GLES20.glViewport(0, 0, mSurfaceWidth, mSurfaceHeight);
			GLES20.glDepthFunc(GLES20.GL_ALWAYS);
			GLES20.glDepthMask(false);
			GLES20.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
			GLES20.glCullFace(GLES20.GL_NONE);

			// Setup projection
			float[] mvp = new float[16];
			Matrix.orthoM(mvp, 0, 0, mSurfaceWidth - 1, mSurfaceHeight - 1, 0, 0, 1);
			GLES20.glUseProgram(mGLProgramId);
			GLES20.glUniformMatrix4fv(mGLProgramHandleMVP, 1, false, mvp, 0);
			GLES20.glUniform1i(mGLProgramHandleTex, 0);

			// Draw display
			drawDisplayTexture();
		}

		private void drawTexturedQuad(int left, int right, int top, int bottom, int texture) {
			ByteBuffer positionBuffer = ByteBuffer.allocateDirect(4 * 8);
			ByteBuffer texCoordBuffer = ByteBuffer.allocateDirect(4 * 8);
			positionBuffer.order(ByteOrder.nativeOrder());
			texCoordBuffer.order(ByteOrder.nativeOrder());

			FloatBuffer positionFloatBuffer = positionBuffer.asFloatBuffer();
			FloatBuffer texCoordFloatBuffer = texCoordBuffer.asFloatBuffer();

			positionFloatBuffer.put((float) left);
			positionFloatBuffer.put((float) top);
			texCoordFloatBuffer.put(0);
			texCoordFloatBuffer.put(0);

			positionFloatBuffer.put((float) left);
			positionFloatBuffer.put((float) bottom);
			texCoordFloatBuffer.put(0);
			texCoordFloatBuffer.put(1);

			positionFloatBuffer.put((float) right);
			positionFloatBuffer.put((float) top);
			texCoordFloatBuffer.put(1);
			texCoordFloatBuffer.put(0);

			positionFloatBuffer.put((float) right);
			positionFloatBuffer.put((float) bottom);
			texCoordFloatBuffer.put(1);
			texCoordFloatBuffer.put(1);

			positionFloatBuffer.position(0);
			GLES20.glEnableVertexAttribArray(0);
			GLES20.glVertexAttribPointer(0, 2, GLES20.GL_FLOAT, false, 8, positionFloatBuffer);
			texCoordFloatBuffer.position(0);
			GLES20.glEnableVertexAttribArray(1);
			GLES20.glVertexAttribPointer(1, 2, GLES20.GL_FLOAT, false, 8, texCoordFloatBuffer);

			GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
			GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
		}

		private void drawDisplayTexture() {
			int dstWidth, dstHeight;
			if (mSurfaceWidth > mSurfaceHeight) {
				// landscape mode
				final float ratio = (float)GBSystem.SCREEN_WIDTH / (float)GBSystem.SCREEN_HEIGHT;
				dstHeight = mSurfaceHeight;
				dstWidth = (int)Math.floor((float)dstHeight * ratio);
			} else {
				// portrait mode
				final float ratio = (float)GBSystem.SCREEN_HEIGHT / (float)GBSystem.SCREEN_WIDTH;
				dstWidth = mSurfaceWidth;
				dstHeight = (int)Math.floor((float)dstWidth * ratio);
			}

			//drawTexturedQuad(0, mSurfaceWidth - 1, 0, mSurfaceHeight - 1, mGLDisplayTexture);
			//drawTexturedQuad(0, GBSystem.SCREEN_WIDTH - 1, 0, GBSystem.SCREEN_HEIGHT - 1, mGLDisplayTexture);
			drawTexturedQuad(0, dstWidth, 0, dstHeight, mGLDisplayTexture);
		}

		private int mGLVertexShaderId = 0;
		private int mGLFragmentShaderId = 0;
		private int mGLProgramId = 0;
		private int mGLProgramHandleMVP = 0;
		private int mGLProgramHandleTex = 0;
		private int mGLDisplayTexture = 0;

		private boolean compileShaders() {
			int mGLVertexShaderId = GLES20.glCreateShader(GLES20.GL_VERTEX_SHADER);
			int mGLFragmentShaderId = GLES20.glCreateShader(GLES20.GL_FRAGMENT_SHADER);
			if (mGLVertexShaderId < 0 || mGLFragmentShaderId < 0) {
				GLES20.glDeleteShader(mGLVertexShaderId);
				GLES20.glDeleteShader(mGLFragmentShaderId);
				return false;
			}

			GLES20.glShaderSource(mGLVertexShaderId, VERTEX_SHADER_SOURCE);
			GLES20.glCompileShader(mGLVertexShaderId);

			GLES20.glShaderSource(mGLFragmentShaderId, FRAGMENT_SHADER_SOURCE);
			GLES20.glCompileShader(mGLFragmentShaderId);

			int[] compileStatus = new int[1];
			GLES20.glGetShaderiv(mGLVertexShaderId, GLES20.GL_COMPILE_STATUS, compileStatus, 0);
			if (compileStatus[0] != GLES20.GL_TRUE) {
				Log.e("GBRenderer", "vertex shader failed compilation: " + GLES20.glGetShaderInfoLog(mGLVertexShaderId));
				return false;
			}
			GLES20.glGetShaderiv(mGLFragmentShaderId, GLES20.GL_COMPILE_STATUS, compileStatus, 0);
			if (compileStatus[0] != GLES20.GL_TRUE) {
				Log.e("GBRenderer", "fragment shader failed compilation: " + GLES20.glGetShaderInfoLog(mGLFragmentShaderId));
				return false;
			}

			mGLProgramId = GLES20.glCreateProgram();
			GLES20.glAttachShader(mGLProgramId, mGLVertexShaderId);
			GLES20.glAttachShader(mGLProgramId, mGLFragmentShaderId);
			GLES20.glBindAttribLocation(mGLProgramId, 0, "vert_position");
			GLES20.glBindAttribLocation(mGLProgramId, 1, "vert_texcoord");
			GLES20.glLinkProgram(mGLProgramId);
			GLES20.glGetProgramiv(mGLProgramId, GLES20.GL_LINK_STATUS, compileStatus, 0);
			if (compileStatus[0] != GLES20.GL_TRUE) {
				Log.e("GBRenderer", "program failed link: " + GLES20.glGetProgramInfoLog(mGLProgramId));
				return false;
			}

			mGLProgramHandleMVP = GLES20.glGetUniformLocation(mGLProgramId, "mvp");
			mGLProgramHandleTex = GLES20.glGetUniformLocation(mGLProgramId, "tex");
			return true;
		}

		private void createGLResources() {
			int[] textures = new int[1];
			GLES20.glGenTextures(1, textures, 0);
			GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textures[0]);
			GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, GBSystem.SCREEN_WIDTH, GBSystem.SCREEN_HEIGHT, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
			GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST);
			GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_NEAREST);
			GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0);
			mGLDisplayTexture = textures[0];
		}

		private void releaseGLResources() {
			int ids[] = new int[0];
			if (mGLDisplayTexture != 0) {
				ids[0] = mGLDisplayTexture;
				GLES20.glDeleteTextures(1, ids, 0);
				mGLDisplayTexture = 0;
			}
			if (mGLProgramId != 0) {
				GLES20.glDeleteProgram(mGLProgramId);
				mGLProgramId = 0;
			}
			if (mGLFragmentShaderId != 0) {
				GLES20.glDeleteShader(mGLFragmentShaderId);
				mGLFragmentShaderId = 0;
			}
			if (mGLVertexShaderId != 0) {
				GLES20.glDeleteShader(mGLVertexShaderId);
				mGLVertexShaderId = 0;
			}
		}

		private final String VERTEX_SHADER_SOURCE =
						"#version 100\n" +
						"uniform mat4 mvp;\n" +
						"attribute vec2 vert_position;\n" +
						"attribute vec2 vert_texcoord;\n" +
						"varying vec2 varying_texcoord;\n" +
						"void main() {\n" +
						"   gl_Position = mvp * vec4(vert_position, 0.0, 1.0);\n" +
						"   varying_texcoord = vert_texcoord;\n" +
						"}\n";

		private final String FRAGMENT_SHADER_SOURCE =
						"#version 100\n" +
						"uniform sampler2D tex;\n" +
						"varying mediump vec2 varying_texcoord;\n" +
						"void main() {" +
						"   gl_FragColor = texture2D(tex, varying_texcoord);\n" +
						//"   gl_FragColor += vec4(varying_texcoord.x, varying_texcoord.y, 0, 1.0);\n" +
						"}\n";


	}


	/* Java Data */
	private GLSurfaceView mGLSurfaceView;
	private Renderer mGBRenderer;
	private Thread workerThread = null;
	private volatile boolean workerThreadRunning = false;

	private void init(GLSurfaceView glSurfaceView) {
		mGLSurfaceView = glSurfaceView;
		mGBRenderer = new Renderer();
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

	public void start(byte[] cartData, boolean frameLimiterEnabled) throws GBSystemException {
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
		nativeSetFrameLimiterEnabled(frameLimiterEnabled);

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
					//Log.d("GBSystem", "wake");
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

    public byte[] saveState(Bitmap screenshot) throws GBSystemException {
        pause();
        byte[] saveData = nativeSaveState();
        nativeCopyScreenBufferToBitmap(screenshot);
        resume();
        return saveData;
    }

    public void loadState(byte[] data) throws GBSystemException {
        pause();
        nativeLoadState(data);
        resume();
    }

	public void setFrameLimiter(boolean enabled) {
		pause();
		nativeSetFrameLimiterEnabled(enabled);
		resume();
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
