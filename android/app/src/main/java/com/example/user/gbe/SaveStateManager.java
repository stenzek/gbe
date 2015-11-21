package com.example.user.gbe;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Environment;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FilenameFilter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;

/**
 * Created by user on 21/11/15.
 */
public class SaveStateManager {
    private String mGamePath;
    private String mSaveBaseFileTitle;
    private String mSaveBaseFilePath;
    private ArrayList<File> mSaveStateFiles;

    public SaveStateManager(String gamePath) {
        mGamePath = gamePath;
        mSaveStateFiles = new ArrayList<File>();

        String fileTitle = (gamePath.lastIndexOf('/') > 0) ? gamePath.substring(gamePath.lastIndexOf('/') + 1) : gamePath;
        if (fileTitle.toLowerCase().endsWith(".gb"))
            fileTitle = fileTitle.substring(0, fileTitle.length() - 4);
        else if (fileTitle.toLowerCase().endsWith(".gbc"))
            fileTitle = fileTitle.substring(0, fileTitle.length() - 5);

        StringBuilder sb = new StringBuilder();
        sb.append(Environment.getExternalStorageDirectory());
        sb.append("/saves/");
        sb.append(fileTitle);
        mSaveBaseFileTitle = fileTitle;
        mSaveBaseFilePath = sb.toString();
        enumerateSaves();
    }

    private void enumerateSaves() {
        File savesDir = new File(Environment.getExternalStorageDirectory() + "/saves");
        File[] saveFiles = savesDir.listFiles(new FilenameFilter() {
            @Override
            public boolean accept(File dir, String filename) {
                return filename.startsWith(mSaveBaseFileTitle) && filename.endsWith(".sav");
            }
        });
        for (int i = 0; i < saveFiles.length; i++) {
            mSaveStateFiles.add(saveFiles[i]);
        }
    }

    private void sortFileList() {
        Collections.sort(mSaveStateFiles, new Comparator<File>() {
            @Override
            public int compare(File lhs, File rhs) {
                return (int)(lhs.lastModified() - rhs.lastModified());
            }
        });
    }

    public void removeSave(File file) {
        if (!mSaveStateFiles.remove(file))
            return;

        file.delete();
    }

    public void saveAuto(byte[] data, Bitmap screenshot) {

    }

    public static class SaveState {
        byte[] mData;
        Bitmap mScreenshot;

        public SaveState(String path) throws IOException {
            FileInputStream stream = null;
            try {
                stream = new FileInputStream(new File(path));

                DataInputStream dstream = new DataInputStream(stream);
                int dataSize = dstream.readInt();
                int bitmapSize = dstream.readInt();

                mData = new byte[dataSize];
                dstream.read(mData);

                byte[] bitmapData = new byte[bitmapSize];
                mScreenshot = BitmapFactory.decodeByteArray(bitmapData, 0, bitmapData.length);

            } catch (FileNotFoundException e) {
                throw new IOException(e.getMessage());
            } catch (IOException e) {
                throw e;
            } finally {
                stream.close();
            }
        }

        public static void writeSaveState(String path, byte[] data, Bitmap screenshot) throws IOException {
            FileOutputStream stream = null;
            try {
                ByteArrayOutputStream bitmapStream = new ByteArrayOutputStream();
                screenshot.compress(Bitmap.CompressFormat.PNG, 0, bitmapStream);
                byte[] bitmapData = bitmapStream.toByteArray();
                bitmapStream.close();

                stream = new FileOutputStream(new File(path));
                DataOutputStream dstream = new DataOutputStream(stream);
                dstream.writeInt(data.length);
                dstream.writeInt(bitmapData.length);
                dstream.write(data);
                dstream.write(bitmapData);
            } catch (FileNotFoundException e) {
                throw new IOException(e.getMessage());
            } catch (IOException e) {
                throw e;
            } finally {
                stream.close();
            }
        }

        byte[] getData() { return mData; }
        Bitmap getScreenshot() { return mScreenshot; }
    }
}
