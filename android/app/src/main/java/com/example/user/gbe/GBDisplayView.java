package com.example.user.gbe;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;

import java.io.IOException;

/**
 * Created by user on 10/11/15.
 */
public class GBDisplayView extends View {
    private Paint mPaint;
    private Bitmap mCurrentBitmap;

    public GBDisplayView(Context context, AttributeSet attributeSet) {
        super(context, attributeSet);

        mPaint = new Paint(0);
        mPaint.setTextSize(128);
    }

    @Override
    protected void onSizeChanged (int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        Log.d("GBDisplayView", "onSizeChanged");
        invalidate();
    }

    @Override
    public void onDraw(Canvas canvas) {
        super.onDraw(canvas);

        canvas.drawColor(Color.BLACK);

        synchronized (this) {
            if (mCurrentBitmap != null) {
                int dstWidth, dstHeight;
                if (getWidth() > getHeight()) {
                    // landscape mode
                    final float ratio = (float)GBSystem.SCREEN_WIDTH / (float)GBSystem.SCREEN_HEIGHT;
                    dstHeight = getHeight();
                    dstWidth = (int)Math.floor((float)dstHeight * ratio);
                } else {
                    // portrait mode
                    final float ratio = (float)GBSystem.SCREEN_HEIGHT / (float)GBSystem.SCREEN_WIDTH;
                    dstWidth = getWidth();
                    dstHeight = (int)Math.floor((float)dstWidth * ratio);
                }

                Rect srcRect = new Rect(0, 0, GBSystem.SCREEN_WIDTH, GBSystem.SCREEN_HEIGHT);
                Rect dstRect = new Rect(0, 0, dstWidth, dstHeight);
                canvas.drawBitmap(mCurrentBitmap, srcRect, dstRect, mPaint);
            }
        }
    }

    public void setDisplayBitmap(Bitmap bitmap) {
        synchronized (this) {
            mCurrentBitmap = bitmap;
        }
        invalidate();
    }
}
