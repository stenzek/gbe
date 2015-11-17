package com.example.user.gbe;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.Toolbar;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import java.io.File;
import java.io.FilenameFilter;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;

public class GameListActivity extends AppCompatActivity {

    static final int ACTIVITY_RESULT_UPDATE_SEARCH_PATHS = 1;

    private RecyclerView mGameListView;
    private GameListAdapter mGameListAdapter;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_game_list);
        Toolbar toolbar = (Toolbar) findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        mGameListView = (RecyclerView)findViewById(R.id.gameListView);
        mGameListView.setLayoutManager(new LinearLayoutManager(this));

        mGameListAdapter = new GameListAdapter(this);
        mGameListView.setAdapter(mGameListAdapter);
        //mGameListAdapter.refreshList();

        mGameListAdapter.setOnItemClickedListener(new GameListAdapter.OnItemClickedListener() {
            public void onClick(GameListAdapter.GameInfo gameInfo) {
                launchGame(gameInfo.getPath());
            }
        });
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_game_list, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();
        switch (id)
        {
            case R.id.action_settings: {
                startActivity(new Intent(this, SettingsActivity.class));
                return true;
            }

            case R.id.edit_rom_paths: {
                startActivityForResult(new Intent(GameListActivity.this, EditGameSearchDirectoriesActivity.class), ACTIVITY_RESULT_UPDATE_SEARCH_PATHS);
                return true;
            }
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        switch (requestCode)
        {
            case ACTIVITY_RESULT_UPDATE_SEARCH_PATHS: {
                if (resultCode == RESULT_OK) {
                    // Refresh the game list.
                    mGameListAdapter.refreshList();
                }
                break;
            }
        }
    }

    /*private void populateGameList()
    {
        // Get current list of paths, split into an array.
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        Set<String> searchDirectories = preferences.getStringSet("romSearchDirectories", new HashSet<String>());

        // Populate the list.
        ListView listView = (ListView)findViewById(R.id.listView);
        if (searchDirectories.size() > 0)
        {
            // Iterate through directories.
            ArrayList<String> romPaths = new ArrayList<String>();
            for (String path : searchDirectories) {
                // Iterate through this directory's files.
                File dir = new File(path);
                File files[] = dir.listFiles(new FilenameFilter() {
                    @Override
                    public boolean accept(File dir, String filename) {
                        //return (filename.toLowerCase().endsWith(".gb") || filename.toLowerCase().endsWith(".gbc"));
                        return true;
                    }
                });
                for (File currentFile : files) {
                    romPaths.add(currentFile.getAbsolutePath());
                }
            }

            // TODO: Extract names.
            final String[] listContents = new String[romPaths.size()];
            romPaths.toArray(listContents);
            ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, listContents);
            listView.setAdapter(adapter);
            listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
                @Override
                public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                    launchGame(listContents[position]);
                }
            });
        }
        else
        {
            // no games
            String[] emptyListContents = new String[] { "No search directories specified." };
            ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, emptyListContents);
            listView.setAdapter(adapter);
            listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
                @Override
                public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                startActivity(new Intent(GameListActivity.this, EditGameSearchDirectoriesActivity.class));
                }
            });
        }
    }*/

    private void launchGame(String path)
    {
        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("romPath", path);
        startActivity(intent);
    }
}
