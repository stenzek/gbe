package com.example.user.gbe;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * Created by user on 20/11/15.
 */
public class CartridgeInfo {
    private static final int SYSTEM_DMG = 0;
    private static final int SYSTEM_SGB = 1;
    private static final int SYSTEM_CGB = 2;

    private String mTitle;
    private boolean mSGBCompatible;
    private boolean mCGBComparible;
    private int mSystemType;

    public CartridgeInfo(String path) throws CartridgeInfoException {
        File file = new File(path);
        FileInputStream stream = null;
        try {
            stream = new FileInputStream(file);
            stream.skip(0x0100);
            parseHeader(stream);
        } catch (FileNotFoundException e) {
            throw new CartridgeInfoException("open error: " + e.getMessage());
        } catch (IOException e) {
            throw new CartridgeInfoException("read error: " + e.getMessage());
        } finally {
            if (stream != null) {
                try {
                    stream.close();
                } catch (IOException e) {
                }
            }
        }
    }

    private void parseHeader(FileInputStream stream) throws CartridgeInfoException, IOException {
        stream.skip(4);     // entrypoint
        stream.skip(48);    // logo

        byte[] cgbTitle = new byte[11];
        byte[] cgbManufacturer = new byte[4];
        byte cgbFlag;
        stream.read(cgbTitle);
        stream.read(cgbManufacturer);
        cgbFlag = (byte)stream.read();

        // parse title based on cgb flags
        StringBuilder titleBuilder = new StringBuilder();
        for (int i = 0; i < cgbTitle.length; i++) {
            if (cgbTitle[i] != 0) {
                titleBuilder.append((char)cgbTitle[i]);
            }
        }
        if ((cgbFlag & 0x80) == 0 && (cgbFlag & 0xC0) == 0) {
            for (int i = 0; i < cgbManufacturer.length; i++) {
                if (cgbManufacturer[i] != 0)
                    titleBuilder.append((char)cgbManufacturer[i]);
            }
            if (cgbFlag != 0)
                titleBuilder.append((char)cgbFlag);
        }
        mTitle = titleBuilder.toString();

        // skip licensee code
        stream.skip(2);
        byte sgbFlag = (byte)stream.read();

        // determine system type
        mCGBComparible = (cgbFlag & 0x80) != 0;
        mSGBCompatible = (sgbFlag & 0x03) != 0;
        if (mCGBComparible)
            mSystemType = SYSTEM_CGB;
        else if (mSGBCompatible)
            mSystemType = SYSTEM_SGB;
        else
            mSystemType = SYSTEM_DMG;

        // remaining bytes
        // TODO parse remaining fields
        stream.skip(9);
    }

    public String getTitle() { return mTitle; }
    public boolean isSGBCompatible() { return mSGBCompatible; }
    public boolean isCGBCompatible() { return mCGBComparible; }
}
