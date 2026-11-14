package io.github.mk0000001.lumaplayer;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/** Draw the paired PC's Blu-ray menu onto a capture-protected surface. */
final class DiscMenuView extends SurfaceView implements SurfaceHolder.Callback {
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private Bitmap image;

    DiscMenuView(Context context) {
        super(context);
        setSecure(true);
        setContentDescription(context.getString(R.string.disc_menu_preview));
        getHolder().addCallback(this);
    }

    static Bitmap decode(byte[] data) {
        if (data == null) return null;
        BitmapFactory.Options options = new BitmapFactory.Options();
        options.inJustDecodeBounds = true;
        BitmapFactory.decodeByteArray(data, 0, data.length, options);
        if (!"image/png".equals(options.outMimeType) || options.outWidth < 1 ||
                options.outHeight < 1 || options.outWidth > 800 || options.outHeight > 450) return null;
        options.inJustDecodeBounds = false;
        return BitmapFactory.decodeByteArray(data, 0, data.length, options);
    }

    void update(Bitmap frame) {
        image = frame;
        drawFrame();
    }

    private void drawFrame() {
        SurfaceHolder holder = getHolder();
        if (!holder.getSurface().isValid()) return;
        Canvas canvas = holder.lockCanvas();
        if (canvas == null) return;
        try {
            canvas.drawColor(Color.BLACK);
            if (image == null) return;
            float scale = Math.min((float) canvas.getWidth() / image.getWidth(),
                    (float) canvas.getHeight() / image.getHeight());
            int width = Math.round(image.getWidth() * scale);
            int height = Math.round(image.getHeight() * scale);
            int x = (canvas.getWidth() - width) / 2;
            int y = (canvas.getHeight() - height) / 2;
            canvas.drawBitmap(image, null, new Rect(x, y, x + width, y + height), paint);
        } finally {
            holder.unlockCanvasAndPost(canvas);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder holder) { drawFrame(); }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { drawFrame(); }
    @Override public void surfaceDestroyed(SurfaceHolder holder) {}
}
