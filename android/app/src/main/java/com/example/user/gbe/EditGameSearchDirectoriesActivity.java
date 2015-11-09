package com.example.user.gbe;

import android.app.Activity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.ListView;
import android.widget.Toast;

import com.nononsenseapps.filepicker.FilePickerActivity;
import com.nononsenseapps.filepicker.FilePickerFragment;

import java.io.File;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Set;

public class EditGameSearchDirectoriesActivity extends AppCompatActivity {
    static final int GET_DIR_CODE = 1;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_edit_game_search_directories);
        Toolbar toolbar = (Toolbar) findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        FloatingActionButton fab = (FloatingActionButton) findViewById(R.id.fab);
        fab.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Intent i = new Intent(EditGameSearchDirectoriesActivity.this, FilePickerActivity.class);
                i.putExtra(FilePickerActivity.EXTRA_ALLOW_MULTIPLE, false);
                i.putExtra(FilePickerActivity.EXTRA_ALLOW_CREATE_DIR, false);
                i.putExtra(FilePickerActivity.EXTRA_MODE, FilePickerActivity.MODE_DIR);
                startActivityForResult(i, GET_DIR_CODE);
            }
        });

        // Fill list.
        populateList();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == GET_DIR_CODE && resultCode == Activity.RESULT_OK) {
            String path = data.getData().getPath();
            addPath(path);
        }
    }

    /*
    private String getSearchDirectoriesString() {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        return preferences.getString("romSearchDirectories", "");
    }

    private Set<String> getSearchDirectoriesSet() {
        // Get current list of paths, split into an array.
        String[] directoriesArray = getSearchDirectoriesString().split(":");
        HashSet<String> directoriesSet = new HashSet<String>();
        Collections.addAll(directoriesSet, directoriesArray);
        return directoriesSet;
    }

    private void setSearchDirectories(String dirs) {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences.Editor editor = preferences.edit();
        editor.putString("romSearchDirectories", dirs);
        editor.commit();
    }

    private void setSearchDirectories(Collection<String> dirs) {
        StringBuilder sb = new StringBuilder();
        Iterator<String> iter = dirs.iterator();
        if (iter.hasNext())
            sb.append(iter.next());
        while (iter.hasNext()) {
            sb.append(":");
            sb.append(iter.next());
        }
        setSearchDirectories(sb.toString());
    }*/
    private Set<String> getSearchDirectories() {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        return preferences.getStringSet("romSearchDirectories", new HashSet<String>());
    }
    private void setSearchDirectories(Set<String> directories) {
        SharedPreferences preferences = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences.Editor editor = preferences.edit();
        editor.putStringSet("romSearchDirectories", directories);
    }

    private void populateList()
    {
        // Get current list of paths, split into an array.
        Set<String> searchDirectories = getSearchDirectories();
        String[] searchDirectoriesArray = new String[searchDirectories.size()];
        searchDirectories.toArray(searchDirectoriesArray);

        // Switch list adapter out.
        ListView listView = (ListView)findViewById(R.id.listView);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, searchDirectoriesArray);
        listView.setAdapter(adapter);
    }

    private void addPath(String path)
    {
        // Get the current list of paths, and add the specified path.
        Set<String> directories = getSearchDirectories();
        directories.add(path);
        setSearchDirectories(directories);
        populateList();
    }

    private void removePath(String path)
    {
        // Get the current list of paths, and remove the specified path.
        Set<String> directories = getSearchDirectories();
        directories.remove(path);
        setSearchDirectories(directories);
        populateList();;
    }
}
