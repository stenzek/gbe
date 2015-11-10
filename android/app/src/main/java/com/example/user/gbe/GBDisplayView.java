package com.example.user.gbe;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.util.AttributeSet;
import android.view.View;

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
    public void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        //canvas.drawText("Hello", 30, 130, mPaint);

        synchronized (this) {
            if (mCurrentBitmap != null) {
                Rect srcRect = new Rect(0, 0, GBSystem.SCREEN_WIDTH, GBSystem.SCREEN_HEIGHT);
                Rect dstRect = new Rect(0, 0, getWidth(), getHeight());
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
