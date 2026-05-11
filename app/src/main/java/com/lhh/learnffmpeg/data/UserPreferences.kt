package com.lhh.learnffmpeg.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map
import javax.inject.Inject
import javax.inject.Singleton

private val Context.appDataStore: DataStore<Preferences> by preferencesDataStore(
    name = "user_prefs"
)

@Singleton
class UserPreferences @Inject constructor(
    @ApplicationContext private val context: Context,
) {
    private val store: DataStore<Preferences> get() = context.appDataStore

    val lastUrl: Flow<String> = store.data.map { it[KEY_LAST_URL].orEmpty() }

    suspend fun setLastUrl(url: String) {
        store.edit { it[KEY_LAST_URL] = url }
    }

    private companion object {
        val KEY_LAST_URL = stringPreferencesKey("last_url")
    }
}
