package com.example.user.gbe;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Environment;
import android.preference.PreferenceManager;
import android.widget.Toast;

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
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;

/**
 * Created by user on 21/11/15.
 */
public class SaveStateManager {
    private Context mContext;
    private String mGamePath;
    private String mSaveBaseFileTitle;
    private String mSaveBaseFilePath;
    private ArrayList<File> mSaveStateFiles;

    public static String getSaveLocation(Context context) {
        File dir = context.getExternalFilesDir(null);
        return (dir != null) ? (dir.getAbsolutePath() + "/saves") : (Environment.getExternalStorageDirectory() + "/saves");
    }

    private static String getBaseFileTitle(String gamePath) {
        String fileTitle = (gamePath.lastIndexOf('/') > 0) ? gamePath.substring(gamePath.lastIndexOf('/') + 1) : gamePath;
        if (fileTitle.toLowerCase().endsWith(".gb"))
            fileTitle = fileTitle.substring(0, fileTitle.length() - 4);
        else if (fileTitle.toLowerCase().endsWith(".gbc"))
            fileTitle = fileTitle.substring(0, fileTitle.length() - 5);

        return fileTitle;
    }

    public SaveStateManager(Context context, String gamePath) {
        mContext = context;
        mGamePath = gamePath;
        mSaveStateFiles = new ArrayList<File>();
        mSaveBaseFileTitle = getBaseFileTitle(gamePath);
        mSaveBaseFilePath = getSaveLocation(context) + "/" + mSaveBaseFileTitle;
        enumerateSaves();
    }

    private void enumerateSaves() {
        mSaveStateFiles.clear();

        File savesDir = new File(Environment.getExternalStorageDirectory() + "/saves");
        File[] saveFiles = savesDir.listFiles(new FilenameFilter() {
            @Override
            public boolean accept(File dir, String filename) {
                return filename.startsWith(mSaveBaseFileTitle) && filename.endsWith(".sav");
            }
        });
        if (saveFiles != null) {
            for (int i = 0; i < saveFiles.length; i++) {
                mSaveStateFiles.add(saveFiles[i]);
            }
            sortFileList();
        }
    }

    private void sortFileList() {
        Collections.sort(mSaveStateFiles, new Comparator<File>() {
            @Override
            public int compare(File lhs, File rhs) {
                return (int) (lhs.lastModified() - rhs.lastModified());
            }
        });
    }

    public void removeSave(File file) {
        if (!mSaveStateFiles.remove(file))
            return;

        file.delete();
    }

    public boolean saveAuto(byte[] data, Bitmap screenshot) {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(mContext);
        //int maxStates = preferences.getInt("max_auto_save_states", 10);
        int maxStates = 10;
        if (maxStates <= 0)
            maxStates = 1;

        // Remove last if we have too many.
        File toRemove = null;
        if (mSaveStateFiles.size() >= maxStates)
            toRemove = mSaveStateFiles.get(mSaveStateFiles.size() - 1);

        // Generate filename.
        Date date = new Date();
        String newStateFileName = mSaveBaseFilePath + "_" + date.getTime() + ".sav";
        try {
            SaveState.writeSaveState(newStateFileName, data, screenshot);
            enumerateSaves();
        } catch (IOException e) {
            return false;
        }

        // Handle deferred remove.
        if (toRemove != null)
            removeSave(toRemove);

        return true;
    }

    public SaveState loadAuto() {
        if (mSaveStateFiles.size() == 0) {
            return null;
        }

        try {
            return new SaveState(mSaveStateFiles.get(0).getAbsolutePath());
        } catch (IOException e) {
            return null;
        }
    }

    public static SaveState getLatestSaveState(Context context, String gamePath) {
        String saveDirectory = getSaveLocation(context);
        final String basePath = getBaseFileTitle(gamePath);
        File saveDirFile = new File(saveDirectory);
        File saveFiles[] = saveDirFile.listFiles(new FilenameFilter() {
            @Override
            public boolean accept(File dir, String filename) {
                return filename.startsWith(basePath) && filename.endsWith(".sav");
            }
        });

        if (saveFiles == null || saveFiles.length == 0)
            return null;

        Arrays.sort(saveFiles, new Comparator<File>() {
            @Override
            public int compare(File lhs, File rhs) {
                return (int) (lhs.lastModified() - rhs.lastModified());
            }
        });

        try {
            return new SaveState(saveFiles[0].getAbsolutePath());
        } catch (IOException e) {
            return null;
        }
    }

    public static class SaveState {
        byte[] mData;
        Bitmap mScreenshot;
        Date mDate;

        public SaveState(String path) throws IOException {
            FileInputStream stream = null;
            try {
                File file = new File(path);
                stream = new FileInputStream(file);

                DataInputStream dstream = new DataInputStream(stream);
                int dataSize = dstream.readInt();
                int bitmapSize = dstream.readInt();

                mData = new byte[dataSize];
                dstream.read(mData);

                byte[] bitmapData = new byte[bitmapSize];
                mScreenshot = BitmapFactory.decodeByteArray(bitmapData, 0, bitmapData.length);
                mDate = new Date(file.lastModified());

            } catch (FileNotFoundException e) {
                throw new IOException(e.getMessage());
            } catch (IOException e) {
                throw e;
            } finally {
                if (stream != null)
                    stream.close();
            }
        }

        public static void writeSaveState(String path, byte[] data, Bitmap screenshot) throws IOException {
            File file = null;
            FileOutputStream stream = null;
            boolean success = false;
            try {
                ByteArrayOutputStream bitmapStream = new ByteArrayOutputStream();
                screenshot.compress(Bitmap.CompressFormat.PNG, 0, bitmapStream);
                byte[] bitmapData = bitmapStream.toByteArray();
                bitmapStream.close();

                stream = new FileOutputStream(path);
                DataOutputStream dstream = new DataOutputStream(stream);
                dstream.writeInt(data.length);
                dstream.writeInt(bitmapData.length);
                dstream.write(data);
                dstream.write(bitmapData);
                success = true;
            } catch (FileNotFoundException e) {
                throw new IOException(e.getMessage());
            } catch (IOException e) {
                throw e;
            } finally {
                if (stream != null) {
                    stream.close();
                    if (!success) {
                        file = new File(path);
                        file.delete();
                    }
                }
            }
        }

        byte[] getData() { return mData; }
        Bitmap getScreenshot() { return mScreenshot; }
        Date getDate() { return mDate; }
    }
}
