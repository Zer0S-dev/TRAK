package com.example.TRAK

import android.annotation.SuppressLint
import android.app.DownloadManager
import android.content.Context
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.view.View
import android.webkit.CookieManager
import android.webkit.JavascriptInterface
import android.webkit.JsResult
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.Toast
import androidx.activity.addCallback
import androidx.appcompat.app.AppCompatActivity
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager

class MainActivity : AppCompatActivity() {

    private val preferences by lazy {
        getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    }

    private lateinit var webView: WebView
    private lateinit var homeView: View
    private lateinit var dashboardUrlInput: EditText

    private companion object {
        const val PREFS_NAME = "trak_preferences"
        const val PREF_DASHBOARD_URL = "dashboard_url"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        setTheme(R.style.Theme_TRAK)
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        webView = findViewById(R.id.webView)
        homeView = findViewById(R.id.homeView)
        dashboardUrlInput = findViewById(R.id.dashboardUrlInput)

        setupWebView()

        findViewById<Button>(R.id.openDashboardButton).setOnClickListener {
            saveAndOpenDashboard()
        }

        onBackPressedDispatcher.addCallback(this) {
            when {
                webView.visibility == View.VISIBLE && webView.canGoBack() ->
                    webView.goBack()

                webView.visibility == View.VISIBLE ->
                    showHome()

                else ->
                    finish()
            }
        }

        val savedUrl = preferences.getString(PREF_DASHBOARD_URL, null)

        if (savedUrl.isNullOrBlank()) {
            showHome()
        } else {
            dashboardUrlInput.setText(savedUrl)
            openDashboard(savedUrl)
        }
    }

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {
        val settings = webView.settings

        settings.javaScriptEnabled = true
        settings.domStorageEnabled = true
        settings.allowFileAccess = true

        CookieManager.getInstance().apply {
            setAcceptCookie(true)
            setAcceptThirdPartyCookies(webView, false)
        }

        webView.addJavascriptInterface(
            TrackerBridge(),
            "Android"
        )

        webView.webViewClient = object : WebViewClient() {
            override fun shouldOverrideUrlLoading(
                view: WebView?,
                url: String?
            ): Boolean {
                return false
            }
        }

        webView.webChromeClient = object : WebChromeClient() {
            override fun onJsAlert(
                view: WebView?,
                url: String?,
                message: String?,
                result: JsResult?
            ): Boolean {
                android.app.AlertDialog.Builder(this@MainActivity)
                    .setTitle("TRAK")
                    .setMessage(message)
                    .setPositiveButton(android.R.string.ok) { _, _ ->
                        result?.confirm()
                    }
                    .setCancelable(false)
                    .show()

                return true
            }
        }

        webView.setDownloadListener { url, _, contentDisposition, mimeType, _ ->
            val request = DownloadManager.Request(Uri.parse(url))
                .setMimeType(mimeType)
                .setTitle(
                    android.webkit.URLUtil.guessFileName(
                        url,
                        contentDisposition,
                        mimeType
                    )
                )
                .setDescription("Téléchargement en cours…")
                .setNotificationVisibility(
                    DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED
                )

            val fileName = android.webkit.URLUtil.guessFileName(
                url,
                contentDisposition,
                mimeType
            )

            request.setDestinationInExternalPublicDir(
                Environment.DIRECTORY_DOWNLOADS,
                fileName
            )

            val manager =
                getSystemService(Context.DOWNLOAD_SERVICE) as DownloadManager

            manager.enqueue(request)

            Toast.makeText(
                this,
                "Téléchargement : $fileName",
                Toast.LENGTH_LONG
            ).show()
        }
    }

    private fun saveAndOpenDashboard() {
        val url = dashboardUrlInput.text
            .toString()
            .trim()
            .removeSuffix("/")

        if (!isValidDashboardUrl(url)) {
            dashboardUrlInput.error =
                "Entrez une adresse http:// ou https:// valide"
            return
        }

        preferences.edit()
            .putString(PREF_DASHBOARD_URL, url)
            .apply()

        openDashboard(url)
    }

    private fun isValidDashboardUrl(url: String): Boolean {
        return try {
            val uri = Uri.parse(url)
            (uri.scheme == "http" || uri.scheme == "https") &&
                !uri.host.isNullOrBlank()
        } catch (_: Exception) {
            false
        }
    }

    private fun openDashboard(url: String) {
        homeView.visibility = View.GONE
        webView.visibility = View.VISIBLE
        webView.loadUrl(url)
    }

    private fun showHome() {
        webView.stopLoading()
        webView.visibility = View.GONE
        homeView.visibility = View.VISIBLE
        dashboardUrlInput.clearFocus()
    }

    inner class TrackerBridge {

        @JavascriptInterface
        fun vibrate(pattern: String?) {
            try {
                val values = pattern
                    ?.split(",")
                    ?.mapNotNull { it.trim().toLongOrNull() }
                    ?.toLongArray()
                    ?: longArrayOf(400L)

                if (values.isEmpty()) return

                if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                    val manager =
                        getSystemService(Context.VIBRATOR_MANAGER_SERVICE)
                                as VibratorManager

                    val vibrator = manager.defaultVibrator
                    if (vibrator.hasVibrator()) {
                        vibrator.vibrate(
                            VibrationEffect.createWaveform(values, -1)
                        )
                    }
                } else {
                    @Suppress("DEPRECATION")
                    val vibrator =
                        getSystemService(Context.VIBRATOR_SERVICE) as Vibrator

                    @Suppress("DEPRECATION")
                    vibrator.vibrate(
                        VibrationEffect.createWaveform(values, -1)
                    )
                }
            } catch (_: Exception) {
                // La vibration ne doit jamais bloquer le dashboard.
            }
        }

        @JavascriptInterface
        fun changeDashboardUrl() {
            runOnUiThread {
                preferences.edit()
                    .remove(PREF_DASHBOARD_URL)
                    .apply()

                dashboardUrlInput.setText("")
                showHome()
                dashboardUrlInput.requestFocus()
            }
        }
    }

    override fun onDestroy() {
        webView.stopLoading()
        webView.destroy()
        super.onDestroy()
    }
}
