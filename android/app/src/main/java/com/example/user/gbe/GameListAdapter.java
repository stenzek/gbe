package com.example.user.gbe;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.preference.PreferenceManager;
import android.support.v7.widget.CardView;
import android.support.v7.widget.RecyclerView;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import java.io.File;
import java.io.FilenameFilter;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;

/**
 * Created by user on 17/11/15.
 */
public class GameListAdapter extends RecyclerView.Adapter<GameListAdapter.ViewHolder> {

    public final class GameInfo {
        private String mPath;
        private boolean mDetailsLoaded;

        private String mTitle;
        private String mFileTitle;
        private Date mLastSaveTime;
        private Bitmap mLastSaveBitmap;

        public GameInfo(String path) {
            mPath = path;
            mFileTitle = (mPath.lastIndexOf('/') > 0) ? mPath.substring(mPath.lastIndexOf('/') + 1) : mPath;
        }

        final String getPath() { return mPath; }
        final String getTitle() { return mTitle; }
        final String getFileTitle() { return mFileTitle; }
        final Date getLastSaveTime() { return mLastSaveTime; }
        final Bitmap getLastSaveBitmap() { return mLastSaveBitmap; }

        final void loadDetails(Context context) {
            if (mDetailsLoaded)
                return;

            try {
                CartridgeInfo info = new CartridgeInfo(mPath);
                mTitle = info.getTitle();
                mLastSaveTime = null;
                mLastSaveBitmap = null;
            } catch (CartridgeInfoException e) {
                // TODO: notifyDataSetChanged when fail?
                mTitle = "<invalid rom>";
                mLastSaveTime = null;
                mLastSaveBitmap = null;
                return;
            }

            SaveStateManager.SaveState latestSaveState = SaveStateManager.getAutoSave(context, mPath);
            if (latestSaveState != null) {
                mLastSaveTime = latestSaveState.getDate();
                mLastSaveBitmap = latestSaveState.getScreenshot();
            } else {
                mLastSaveTime = null;
                mLastSaveBitmap = null;
            }

            mDetailsLoaded = true;
        }
    }

    public interface OnItemClickedListener {
        public void onClick(GameInfo gameInfo);
    }
    public interface OnItemLongClickedListener {
        public boolean onLongClick(GameInfo gameInfo);
    }

    private Context mContext;
    private ArrayList<GameInfo> mGameInfoList;
    private OnItemClickedListener mOnItemClickedListener;
    private OnItemLongClickedListener mOnItemLongClickedListener;

    public GameListAdapter(Context context) {
        mContext = context;
        mGameInfoList = new ArrayList<GameInfo>();
        populateList();
    }

    public void refreshList() {
        if (mGameInfoList.size() > 0) {
            for (int i = mGameInfoList.size() - 1; i >= 0; i--) {
                notifyItemRemoved(i);
            }
        }
        mGameInfoList.clear();

        populateList();
    }

    private void populateList() {
        // Get current list of paths, split into an array.
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(mContext);
        Set<String> searchDirectories = preferences.getStringSet("romSearchDirectories", new HashSet<String>());

        // Iterate through directories.
        ArrayList<String> romPaths = new ArrayList<String>();
        for (String path : searchDirectories) {
            // Iterate through this directory's files.
            File dir = new File(path);
            File files[] = dir.listFiles(new FilenameFilter() {
                @Override
                public boolean accept(File dir, String filename) {
                    return (filename.toLowerCase().endsWith(".gb") || filename.toLowerCase().endsWith(".gbc"));
                }
            });
            for (File currentFile : files) {
                GameInfo gi = new GameInfo(currentFile.getAbsolutePath());

                int newPosition = mGameInfoList.size();
                mGameInfoList.add(gi);
                notifyItemInserted(newPosition);
            }
        }
    }

    public void setOnItemClickedListener(OnItemClickedListener listener) {
        mOnItemClickedListener = listener;
    }
    public void setOnItemLongClickedListener(OnItemLongClickedListener listener) {
        mOnItemLongClickedListener = listener;
    }

    public final class ViewHolder extends RecyclerView.ViewHolder {
        private CardView mCardView;
        private GameInfo mGameInfo;
        private ImageView mGameImageView;
        private TextView mGameTitleView;
        private TextView mGameFileView;
        private TextView mGameSummaryView;

        public ViewHolder(View itemView) {
            super(itemView);
            mCardView = (CardView)itemView;
            mGameImageView = (ImageView)mCardView.findViewById(R.id.game_image);
            mGameTitleView = (TextView)mCardView.findViewById(R.id.game_title);
            mGameFileView = (TextView)mCardView.findViewById(R.id.game_filename);
            mGameSummaryView = (TextView)mCardView.findViewById(R.id.game_details);

            mCardView.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    if (GameListAdapter.this.mOnItemClickedListener != null) {
                        GameListAdapter.this.mOnItemClickedListener.onClick(mGameInfo);
                    }
                }
            });
            mCardView.setOnLongClickListener(new View.OnLongClickListener() {
                @Override
                public boolean onLongClick(View v) {
                    if (GameListAdapter.this.mOnItemLongClickedListener != null) {
                        return GameListAdapter.this.mOnItemLongClickedListener.onLongClick(mGameInfo);
                    }
                    return false;
                }
            });
        }

        public void bindGameInfo(GameInfo gi) {
            mGameInfo = gi;
            if (gi != null) {
                gi.loadDetails(mContext);
                mGameTitleView.setText(gi.getTitle());
                mGameFileView.setText(gi.getFileTitle());

                Date lastSaveTime = gi.getLastSaveTime();
                String summaryText;
                if (lastSaveTime != null) {
                    summaryText = String.format("Last played: %s", lastSaveTime.toString());
                } else {
                    summaryText = "Never played";
                }
                mGameSummaryView.setText(summaryText);

                if (gi.getLastSaveBitmap() != null)
                    mGameImageView.setImageBitmap(gi.getLastSaveBitmap());
                else
                    mGameImageView.setImageDrawable(mCardView.getResources().getDrawable(android.R.drawable.sym_def_app_icon));
            }
        }
    }

    @Override
    public ViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        View itemView = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.game_list_item, parent, false);

        return new ViewHolder(itemView);
    }

    @Override
    public void onBindViewHolder(ViewHolder holder, int position) {
        holder.bindGameInfo(mGameInfoList.get(position));
    }

    @Override
    public int getItemCount() {
        return mGameInfoList.size();
    }
}
