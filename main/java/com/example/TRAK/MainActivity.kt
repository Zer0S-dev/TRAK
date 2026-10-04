package com.example.TRAK

import android.webkit.WebResourceResponse
import android.webkit.WebResourceRequest
import android.Manifest
import android.annotation.SuppressLint
import android.app.DownloadManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.net.Uri
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Bundle
import android.os.Environment
import android.webkit.*
import android.widget.Toast
import androidx.activity.addCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.NetworkInterface
import java.net.URL
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager

class MainActivity : AppCompatActivity() {

    private val preferences by lazy {
        getSharedPreferences(
            "trak_preferences",
            Context.MODE_PRIVATE
        )
    }

    private companion object {

        const val PREF_LAST_MODE =
            "last_connection_mode"

        const val PREF_LAST_LOCAL_IP =
            "last_local_ip"

        /*
         * Token de session mémorisé.
         *
         * On ne stocke jamais le mot de passe.
         */
        const val PREF_SESSION_COOKIE =
            "trak_session_cookie"

        const val MODE_LOCAL =
            "local"

        const val MODE_DIRECT =
            "direct"

        /*
         * Adresse du tracker en WiFi Direct.
         */
        const val DIRECT_IP =
            "192.168.4.1"

        const val DIRECT_URL =
            "http://192.168.4.1/"

        /*
         * Temps maximum d'attente d'une connexion.
         */
        const val LOCAL_TIMEOUT_MS =
            8000L

        /*
         * En mode Wi-Fi Direct, le dashboard reste sur le TRAK
         * (192.168.4.1) mais les tuiles OpenStreetMap passent
         * explicitement par la 4G du téléphone.
         */
        const val OSM_TILE_HOST =
            "tile.openstreetmap.org"

        const val OSM_TILE_TIMEOUT_MS =
            10000
    }

    private lateinit var webView: WebView

    /*
     * Réseau cellulaire conservé pour les requêtes Internet
     * spécifiques aux tuiles OSM en mode Wi-Fi Direct.
     */
    private var cellularNetwork: Network? = null
    private var cellularNetworkCallback: ConnectivityManager.NetworkCallback? = null

    private val LOCATION_PERMISSION_REQUEST_CODE =
        1001

    /*
     * Recherche du tracker.
     */
    private val scanExecutor =
        Executors.newFixedThreadPool(32)

    /*
     * Permet d'arrêter rapidement les recherches.
     */
    private var scanRunning =
        AtomicBoolean(false)


    // ============================================================
    // LIFECYCLE
    // ============================================================

    override fun onCreate(
        savedInstanceState: Bundle?
    ) {

        setTheme(R.style.Theme_TRAK)

        super.onCreate(savedInstanceState)

        setContentView(
            R.layout.activity_main
        )

        onBackPressedDispatcher.addCallback(this) {

            if (
                ::webView.isInitialized &&
                webView.canGoBack()
            ) {

                webView.goBack()

            } else {

                finish()
            }
        }

        if (
            ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.ACCESS_FINE_LOCATION
            ) != PackageManager.PERMISSION_GRANTED
        ) {

            ActivityCompat.requestPermissions(
                this,
                arrayOf(
                    Manifest.permission.ACCESS_FINE_LOCATION
                ),
                LOCATION_PERMISSION_REQUEST_CODE
            )

        } else {

            setupWebView()
        }
    }


    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {

        super.onRequestPermissionsResult(
            requestCode,
            permissions,
            grantResults
        )

        if (
            requestCode ==
            LOCATION_PERMISSION_REQUEST_CODE &&
            grantResults.isNotEmpty() &&
            grantResults[0] ==
            PackageManager.PERMISSION_GRANTED
        ) {

            setupWebView()

        } else {

            Toast.makeText(
                this,
                "Location permission denied",
                Toast.LENGTH_SHORT
            ).show()
        }
    }


    // ============================================================
    // WEBVIEW
    // ============================================================

    @SuppressLint("SetJavaScriptEnabled")
    private fun setupWebView() {

        webView =
            findViewById(R.id.webView)

        android.util.Log.d(
            "TRAK_TEST",
            "=== TRAK MainActivity / WebView démarrée ==="
        )

        val webSettings =
            webView.settings

        webSettings.javaScriptEnabled =
            true

        webSettings.domStorageEnabled =
            true

        webSettings.setGeolocationEnabled(
            true
        )

        webSettings.allowFileAccess =
            true

        webSettings.allowUniversalAccessFromFileURLs =
            true


        // ========================================================
        // COOKIES
        // ========================================================

        val cookieManager =
            CookieManager.getInstance()

        cookieManager.setAcceptCookie(
            true
        )

        cookieManager.setAcceptThirdPartyCookies(
            webView,
            false
        )


        // ========================================================
        // JAVASCRIPT -> ANDROID
        // ========================================================

        webView.addJavascriptInterface(
            TrackerBridge(),
            "Android"
        )


        // ========================================================
        // WEBVIEW CLIENT
        // ========================================================

        webView.webViewClient =
            object : WebViewClient() {

                /*
                 * Wi-Fi Direct isole la WebView d'Internet.
                 * Le dashboard doit cependant continuer à charger
                 * les tuiles OpenStreetMap via la 4G du téléphone.
                 *
                 * On intercepte uniquement les tuiles OSM et uniquement
                 * lorsque le dashboard est connecté au TRAK en Direct.
                 * Les requêtes vers 192.168.4.1 restent sur le Wi-Fi Direct.
                 */
                override fun shouldInterceptRequest(
                    view: WebView?,
                    request: WebResourceRequest
                ): WebResourceResponse? {

                    val uri = request.url
                    val scheme = uri.scheme?.lowercase()
                    val url = uri.toString()

                    // Le dashboard TRAK reste sur le Wi-Fi Direct.
                    if (isTrackerUrl(url) || !isDirectMode()) {
                        return null
                    }

                    // En mode Direct, toute ressource HTTP/HTTPS externe
                    // est explicitement chargée via le réseau cellulaire.
                    if (scheme != "http" && scheme != "https") {
                        return null
                    }

                    val network = cellularNetwork
                    if (network == null) {
                        android.util.Log.w(
                            "TRAK_NET",
                            "4G indisponible pour la ressource externe : $url"
                        )
                        return null
                    }

                    return try {
                        openExternalThroughCellular(
                            network,
                            uri,
                            request.requestHeaders
                        )
                    } catch (e: Exception) {
                        android.util.Log.w(
                            "TRAK_NET",
                            "Erreur chargement 4G : ${e.message}"
                        )
                        null
                    }
                }

                override fun onPageFinished(
                    view: WebView?,
                    url: String?
                ) {

                    android.util.Log.d(
                        "TRAK_SESSION",
                        "Page chargée : $url"
                    )

                    /*
                     * Si la page appartient au tracker,
                     * on mémorise automatiquement le cookie.
                     */
                    if (
                        url != null &&
                        isTrackerUrl(url)
                    ) {

                        rememberSessionCookie(
                            url
                        )
                    }

                    persistCookies()

                    super.onPageFinished(
                        view,
                        url
                    )
                }


                override fun shouldOverrideUrlLoading(
                    view: WebView?,
                    url: String?
                ): Boolean {

                    if (url == null)
                        return false


                    if (
                        url.startsWith(
                            "intent://"
                        )
                    ) {

                        try {

                            val intent =
                                Intent.parseUri(
                                    url,
                                    Intent.URI_INTENT_SCHEME
                                )

                            if (
                                intent.resolveActivity(
                                    packageManager
                                ) != null
                            ) {

                                startActivity(
                                    intent
                                )

                            } else {

                                val fallbackUrl =
                                    intent.getStringExtra(
                                        "browser_fallback_url"
                                    )

                                if (
                                    fallbackUrl != null
                                ) {

                                    webView.loadUrl(
                                        fallbackUrl
                                    )
                                }
                            }

                            return true

                        } catch (
                            e: Exception
                        ) {

                            e.printStackTrace()

                            return true
                        }
                    }

                    return false
                }
            }


        // ========================================================
        // WEB CHROME
        // ========================================================

        webView.webChromeClient =
            object : WebChromeClient() {

                override fun onGeolocationPermissionsShowPrompt(
                    origin: String?,
                    callback: GeolocationPermissions.Callback?
                ) {

                    callback?.invoke(
                        origin,
                        true,
                        false
                    )
                }


                override fun onJsAlert(
                    view: WebView?,
                    url: String?,
                    message: String?,
                    result: JsResult?
                ): Boolean {

                    val builder =
                        android.app.AlertDialog.Builder(
                            this@MainActivity
                        )

                    builder.setTitle(
                        "TRAK"
                    )

                    builder.setMessage(
                        message
                    )

                    builder.setPositiveButton(
                        android.R.string.ok
                    ) { _, _ ->

                        result?.confirm()
                    }

                    builder.setCancelable(
                        false
                    )

                    builder.create().show()

                    return true
                }
            }


        // ========================================================
        // RÉSEAU MOBILE POUR OSM
        // ========================================================
        // Uniquement si la dernière connexion était en Wi-Fi Direct.
        // La 4G n'est jamais liée à toute la WebView.
        if (isDirectMode()) {
            requestCellularNetwork()
        }


        // ========================================================
        // DOWNLOADS
        // ========================================================

        webView.setDownloadListener {
                url,
                userAgent,
                contentDisposition,
                mimeType,
                contentLength ->

            val request =
                DownloadManager.Request(
                    Uri.parse(url)
                )

            request.setMimeType(
                mimeType
            )

            val fileName =
                URLUtil.guessFileName(
                    url,
                    contentDisposition,
                    mimeType
                )

            request.setTitle(
                fileName
            )

            request.setDescription(
                "Téléchargement en cours…"
            )

            request.setNotificationVisibility(
                DownloadManager.Request
                    .VISIBILITY_VISIBLE_NOTIFY_COMPLETED
            )

            request.setDestinationInExternalPublicDir(
                Environment.DIRECTORY_DOWNLOADS,
                fileName
            )

            val dm =
                getSystemService(
                    Context.DOWNLOAD_SERVICE
                ) as DownloadManager

            dm.enqueue(
                request
            )

            Toast.makeText(
                this,
                "Téléchargement : $fileName",
                Toast.LENGTH_LONG
            ).show()
        }


        // ========================================================
        // ÉCRAN D'ACCUEIL
        // ========================================================

        webView.loadUrl(
            "file:///android_asset/index.html"
        )
    }


    // ============================================================
    // GESTION SESSION TRACKER
    // ============================================================

    /*
     * Vérifie si une URL appartient au tracker.
     *
     * On accepte :
     *
     * 192.168.4.1
     * dernière IP locale connue
     * trak.local
     */
    private fun isTrackerUrl(
        url: String
    ): Boolean {

        if (
            url.startsWith(
                "http://$DIRECT_IP/"
            )
        ) {
            return true
        }

        if (
            url.startsWith(
                "http://trak.local/"
            )
        ) {
            return true
        }

        val localIp =
            preferences.getString(
                PREF_LAST_LOCAL_IP,
                null
            )

        if (
            localIp != null &&
            url.startsWith(
                "http://$localIp/"
            )
        ) {
            return true
        }

        return false
    }


    /*
     * Extrait le cookie TRAK_SESSION d'une origine.
     */
    private fun getSessionCookie(
        url: String
    ): String? {

        return try {

            val cookies =
                CookieManager
                    .getInstance()
                    .getCookie(url)

            if (
                cookies.isNullOrBlank()
            ) {

                null

            } else {

                cookies
                    .split(";")
                    .map {
                        it.trim()
                    }
                    .firstOrNull {
                        it.startsWith(
                            "TRAK_SESSION="
                        )
                    }
            }

        } catch (
            e: Exception
        ) {

            android.util.Log.e(
                "TRAK_SESSION",
                "Erreur lecture cookie",
                e
            )

            null
        }
    }


    /*
     * Mémorise le cookie de session.
     *
     * IMPORTANT :
     *
     * On ne mémorise jamais le mot de passe.
     * Seul le token TRAK_SESSION est conservé.
     */
    private fun rememberSessionCookie(
        url: String
    ) {

        val cookie =
            getSessionCookie(
                url
            )

        if (
            cookie != null
        ) {

            preferences
                .edit()
                .putString(
                    PREF_SESSION_COOKIE,
                    cookie
                )
                .apply()

            android.util.Log.d(
                "TRAK_SESSION",
                "Session TRAK mémorisée pour changement de réseau"
            )
        }
    }


    /*
     * Recherche une session existante.
     *
     * Ordre :
     *
     * 1. URL actuellement chargée
     * 2. WiFi Direct
     * 3. dernière IP locale connue
     * 4. copie persistante SharedPreferences
     *
     * Cette dernière étape permet le fonctionnement
     * après kill / redémarrage de l'application.
     */
    private fun findExistingSessionCookie():
            String? {

        // --------------------------------------------------------
        // 1. URL ACTUELLE
        // --------------------------------------------------------

        val currentUrl =
            if (
                ::webView.isInitialized
            ) {
                webView.url
            } else {
                null
            }

        if (
            currentUrl != null &&
            isTrackerUrl(currentUrl)
        ) {

            val cookie =
                getSessionCookie(
                    currentUrl
                )

            if (
                cookie != null
            ) {

                return cookie
            }
        }


        // --------------------------------------------------------
        // 2. WIFI DIRECT
        // --------------------------------------------------------

        val directCookie =
            getSessionCookie(
                DIRECT_URL
            )

        if (
            directCookie != null
        ) {

            return directCookie
        }


        // --------------------------------------------------------
        // 3. DERNIÈRE IP LOCALE
        // --------------------------------------------------------

        val localIp =
            preferences.getString(
                PREF_LAST_LOCAL_IP,
                null
            )

        if (
            localIp != null
        ) {

            val localCookie =
                getSessionCookie(
                    "http://$localIp/"
                )

            if (
                localCookie != null
            ) {

                return localCookie
            }
        }


        // --------------------------------------------------------
        // 4. COPIE PERSISTANTE
        // --------------------------------------------------------

        return preferences.getString(
            PREF_SESSION_COOKIE,
            null
        )
    }


    /*
     * Transfère le cookie vers une nouvelle origine.
     *
     * CORRECTIF IMPORTANT :
     *
     * Ancien :
     *
     * SameSite=Strict
     *
     * Nouveau :
     *
     * SameSite=Lax
     *
     * Local et Direct sont deux origines différentes :
     *
     * 192.168.1.30
     *        ↓
     * 192.168.4.1
     *
     * SameSite=Lax permet au cookie d'être
     * envoyé lors de la navigation initiale
     * vers la nouvelle adresse.
     */
    private fun copySessionCookieTo(
        targetUrl: String,
        callback: () -> Unit
    ) {

        val cookie =
            findExistingSessionCookie()

        if (
            cookie.isNullOrBlank()
        ) {

            android.util.Log.d(
                "TRAK_SESSION",
                "Aucune session existante à transférer"
            )

            callback()
            return
        }


        android.util.Log.d(
            "TRAK_SESSION",
            "Transfert de la session vers : $targetUrl"
        )


        /*
         * CookieManager attend uniquement :
         *
         * TRAK_SESSION=xxxxxxxx
         *
         * On reconstruit ensuite les attributs.
         */
        val cookieValue =
            if (
                cookie.startsWith(
                    "TRAK_SESSION="
                )
            ) {

                cookie.substring(
                    "TRAK_SESSION=".length
                )

            } else {

                cookie
            }


        val newCookie =
            "TRAK_SESSION=$cookieValue; " +
                    "Path=/; " +
                    "HttpOnly; " +
                    "SameSite=Lax"


        CookieManager
            .getInstance()
            .setCookie(
                targetUrl,
                newCookie
            ) {

                CookieManager
                    .getInstance()
                    .flush()


                /*
                 * Vérification immédiate.
                 */
                val verification =
                    getSessionCookie(
                        targetUrl
                    )


                if (
                    verification != null
                ) {

                    android.util.Log.d(
                        "TRAK_SESSION",
                        "Cookie transféré et vérifié sur : $targetUrl"
                    )

                } else {

                    android.util.Log.w(
                        "TRAK_SESSION",
                        "ATTENTION : cookie non retrouvé après transfert vers : $targetUrl"
                    )
                }


                runOnUiThread {

                    callback()
                }
            }
    }


    // ============================================================
    // COOKIES
    // ============================================================

    private fun persistCookies() {

        CookieManager
            .getInstance()
            .flush()

        android.util.Log.d(
            "TRAK_SESSION",
            "Cookies WebView sauvegardés"
        )
    }


    // ============================================================
    // CONNEXION AUTOMATIQUE AU TRACKER
    // ============================================================

    private fun connectToTracker(
        callback: (String?) -> Unit
    ) {

        if (
            scanRunning.getAndSet(true)
        ) {

            return
        }

        Thread {

            try {

                android.util.Log.d(
                    "TRAK_SCAN",
                    "Recherche automatique du tracker..."
                )


                // ------------------------------------------------
                // 1. TEST WIFI DIRECT
                // ------------------------------------------------

                val localIp =
                    getLocalIPv4()

                android.util.Log.d(
                    "TRAK_SCAN",
                    "IP du téléphone : $localIp"
                )

                if (
                    localIp != null &&
                    localIp.startsWith(
                        "192.168.4."
                    )
                ) {

                    android.util.Log.d(
                        "TRAK_SCAN",
                        "Réseau WiFi Direct détecté"
                    )

                    if (
                        isTracker(
                            DIRECT_IP
                        )
                    ) {

                        android.util.Log.d(
                            "TRAK_SCAN",
                            "Tracker Direct identifié : $DIRECT_IP"
                        )

                        preferences
                            .edit()
                            .putString(
                                PREF_LAST_MODE,
                                MODE_DIRECT
                            )
                            .apply()

                        requestCellularNetwork()

                        runOnUiThread {

                            scanRunning.set(
                                false
                            )

                            callback(
                                DIRECT_IP
                            )
                        }

                        return@Thread
                    }

                    android.util.Log.d(
                        "TRAK_SCAN",
                        "192.168.4.1 ne répond pas comme un tracker"
                    )
                }


                // ------------------------------------------------
                // 2. RECHERCHE RESEAU LOCAL
                // ------------------------------------------------

                android.util.Log.d(
                    "TRAK_SCAN",
                    "Recherche sur le réseau WiFi local..."
                )

                findTrackerInternal { ip ->

                    runOnUiThread {

                        scanRunning.set(
                            false
                        )

                        if (
                            ip != null
                        ) {

                            preferences
                                .edit()
                                .putString(
                                    PREF_LAST_MODE,
                                    MODE_LOCAL
                                )
                                .putString(
                                    PREF_LAST_LOCAL_IP,
                                    ip
                                )
                                .apply()
                        }

                        callback(
                            ip
                        )
                    }
                }

            } catch (
                e: Exception
            ) {

                android.util.Log.e(
                    "TRAK_SCAN",
                    "Erreur connexion automatique",
                    e
                )

                runOnUiThread {

                    scanRunning.set(
                        false
                    )

                    callback(
                        null
                    )
                }
            }

        }.start()
    }


    // ============================================================
    // RECHERCHE TRACKER RESEAU LOCAL
    // ============================================================

    private fun findTrackerInternal(
        callback: (String?) -> Unit
    ) {

        Thread {

            try {

                // ------------------------------------------------
                // 1. DERNIÈRE IP CONNUE
                // ------------------------------------------------

                val cachedIp =
                    preferences.getString(
                        PREF_LAST_LOCAL_IP,
                        null
                    )

                if (
                    cachedIp != null &&
                    isTracker(
                        cachedIp
                    )
                ) {

                    android.util.Log.d(
                        "TRAK_SCAN",
                        "Tracker retrouvé dans le cache : $cachedIp"
                    )

                    runOnUiThread {

                        callback(
                            cachedIp
                        )
                    }

                    return@Thread
                }


                // ------------------------------------------------
                // 2. DETECTION RESEAU
                // ------------------------------------------------

                val network =
                    getLocalNetwork()

                if (
                    network == null
                ) {

                    android.util.Log.d(
                        "TRAK_SCAN",
                        "Impossible de déterminer le réseau local"
                    )

                    runOnUiThread {
                        callback(null)
                    }

                    return@Thread
                }


                val baseIp =
                    network.first

                val prefix =
                    network.second


                android.util.Log.d(
                    "TRAK_SCAN",
                    "Réseau détecté : $baseIp/$prefix"
                )


                val parts =
                    baseIp.split(".")

                if (
                    parts.size != 4
                ) {

                    runOnUiThread {
                        callback(null)
                    }

                    return@Thread
                }


                /*
                 * Pour l'instant on conserve
                 * le fonctionnement /24.
                 */
                val prefixBase =
                    "${parts[0]}.${parts[1]}.${parts[2]}"


                android.util.Log.d(
                    "TRAK_SCAN",
                    "Scan : $prefixBase.1 -> $prefixBase.254"
                )


                val foundIp =
                    AtomicReference<String?>(
                        null
                    )


                val executor =
                    Executors.newFixedThreadPool(
                        32
                    )


                for (
                i in 1..254
                ) {

                    if (
                        !scanRunning.get()
                    ) {
                        break
                    }


                    val ip =
                        "$prefixBase.$i"


                    executor.submit {

                        if (
                            scanRunning.get() &&
                            foundIp.get() == null
                        ) {

                            if (
                                isTracker(
                                    ip
                                )
                            ) {

                                if (
                                    foundIp.compareAndSet(
                                        null,
                                        ip
                                    )
                                ) {

                                    android.util.Log.d(
                                        "TRAK_SCAN",
                                        "Tracker trouvé : $ip"
                                    )

                                    preferences
                                        .edit()
                                        .putString(
                                            PREF_LAST_LOCAL_IP,
                                            ip
                                        )
                                        .apply()

                                    scanRunning.set(
                                        false
                                    )
                                }
                            }
                        }
                    }
                }


                executor.shutdown()


                executor.awaitTermination(
                    8,
                    TimeUnit.SECONDS
                )


                val result =
                    foundIp.get()


                runOnUiThread {

                    callback(
                        result
                    )
                }


            } catch (
                e: Exception
            ) {

                android.util.Log.e(
                    "TRAK_SCAN",
                    "Erreur recherche tracker",
                    e
                )

                runOnUiThread {

                    callback(
                        null
                    )
                }
            }

        }.start()
    }


    // ============================================================
    // TEST TRACKER
    // ============================================================

    private fun isTracker(
        ip: String
    ): Boolean {

        var connection:
                HttpURLConnection? = null

        try {

            val url =
                URL(
                    "http://$ip/api/data"
                )


            connection =
                url.openConnection()
                        as HttpURLConnection


            connection.connectTimeout =
                500

            connection.readTimeout =
                700

            connection.requestMethod =
                "GET"

            connection.instanceFollowRedirects =
                false


            val responseCode =
                connection.responseCode


            if (
                responseCode != 401
            ) {

                return false
            }


            val stream =
                connection.errorStream
                    ?: connection.inputStream


            val reader =
                BufferedReader(
                    InputStreamReader(
                        stream
                    )
                )


            val response =
                reader.readText()


            reader.close()


            val isTracker =
                response.contains(
                    "authentication required"
                )


            if (
                isTracker
            ) {

                android.util.Log.d(
                    "TRAK_SCAN",
                    "Tracker identifié : $ip"
                )
            }


            return isTracker


        } catch (
            _: Exception
        ) {

            return false

        } finally {

            connection?.disconnect()
        }
    }


    // ============================================================
    // IP WIFI DU TELEPHONE
    // ============================================================

    private fun getLocalIPv4():
            String? {

        try {

            val interfaces =
                NetworkInterface
                    .getNetworkInterfaces()


            while (
                interfaces.hasMoreElements()
            ) {

                val networkInterface =
                    interfaces.nextElement()


                if (
                    networkInterface.isLoopback ||
                    !networkInterface.isUp
                ) {
                    continue
                }


                val name =
                    networkInterface.name


                if (
                    !name.equals(
                        "wlan0",
                        ignoreCase = true
                    ) &&
                    !name.equals(
                        "swlan0",
                        ignoreCase = true
                    )
                ) {
                    continue
                }


                val addresses =
                    networkInterface
                        .inetAddresses


                while (
                    addresses.hasMoreElements()
                ) {

                    val address =
                        addresses.nextElement()


                    if (
                        address
                                is Inet4Address &&
                        !address.isLoopbackAddress
                    ) {

                        return address.hostAddress
                    }
                }
            }

        } catch (
            e: Exception
        ) {

            android.util.Log.e(
                "TRAK_SCAN",
                "Erreur récupération IP WiFi",
                e
            )
        }


        return null
    }


    // ============================================================
    // DETECTION RESEAU LOCAL
    // ============================================================

    private fun getLocalNetwork():
            Pair<String, Int>? {

        try {

            val interfaces =
                NetworkInterface
                    .getNetworkInterfaces()


            while (
                interfaces.hasMoreElements()
            ) {

                val networkInterface =
                    interfaces.nextElement()


                if (
                    networkInterface.isLoopback ||
                    !networkInterface.isUp
                ) {
                    continue
                }


                val name =
                    networkInterface.name


                if (
                    !name.equals(
                        "wlan0",
                        ignoreCase = true
                    ) &&
                    !name.equals(
                        "swlan0",
                        ignoreCase = true
                    )
                ) {
                    continue
                }


                val addresses =
                    networkInterface
                        .interfaceAddresses


                for (
                address in addresses
                ) {

                    val inetAddress =
                        address.address


                    if (
                        inetAddress
                                !is Inet4Address
                    ) {
                        continue
                    }


                    if (
                        inetAddress
                            .isLoopbackAddress
                    ) {
                        continue
                    }


                    val ip =
                        inetAddress
                            .hostAddress


                    val prefix =
                        address
                            .networkPrefixLength
                            .toInt()


                    if (
                        prefix <= 0 ||
                        prefix > 30
                    ) {
                        continue
                    }


                    return Pair(
                        ip,
                        prefix
                    )
                }
            }

        } catch (
            e: Exception
        ) {

            android.util.Log.e(
                "TRAK_SCAN",
                "Erreur réseau",
                e
            )
        }


        return null
    }


    // ============================================================
    // JAVASCRIPT -> ANDROID
    // ============================================================

    inner class TrackerBridge {

        /*
         * Nouvelle commande :
         *
         * Android.connectToTracker()
         *
         * Le JavaScript ne choisit plus
         * Local ou Direct.
         */
        @JavascriptInterface
        fun connectToTracker() {

            android.util.Log.d(
                "TRAK_SCAN",
                "Connexion automatique demandée par l'interface"
            )

            runOnUiThread {

                webView.evaluateJavascript(
                    "window.trackerSearching && window.trackerSearching();",
                    null
                )

                connectToTracker { ip ->

                    if (
                        ip != null
                    ) {

                        val trackerUrl =
                            "http://$ip/"


                        android.util.Log.d(
                            "TRAK_SCAN",
                            "Tracker trouvé : $trackerUrl"
                        )


                        webView.evaluateJavascript(
                            "window.trackerFound && window.trackerFound();",
                            null
                        )


                        /*
                         * ------------------------------------------------
                         * TRANSFERT SESSION
                         * ------------------------------------------------
                         */
                        copySessionCookieTo(
                            trackerUrl
                        ) {

                            webView.postDelayed({

                                webView.loadUrl(
                                    trackerUrl
                                )

                            }, 500)
                        }

                    } else {

                        android.util.Log.d(
                            "TRAK_SCAN",
                            "Tracker introuvable"
                        )

                        webView.evaluateJavascript(
                            "window.trackerNotFound && window.trackerNotFound();",
                            null
                        )
                    }
                }
            }
        }


        /*
         * Ancienne fonction conservée temporairement.
         */
        @JavascriptInterface
        fun loadDashboard(
            url: String
        ) {

            runOnUiThread {

                if (
                    url ==
                    DIRECT_URL
                ) {

                    preferences
                        .edit()
                        .putString(
                            PREF_LAST_MODE,
                            MODE_DIRECT
                        )
                        .apply()

                    requestCellularNetwork()

                    copySessionCookieTo(
                        DIRECT_URL
                    ) {

                        webView.loadUrl(
                            DIRECT_URL
                        )
                    }

                    return@runOnUiThread
                }


                if (
                    url ==
                    "http://trak.local/"
                ) {

                    preferences
                        .edit()
                        .putString(
                            PREF_LAST_MODE,
                            MODE_LOCAL
                        )
                        .apply()

                    releaseCellularNetwork()

                    findTracker { ip ->

                        if (
                            ip != null
                        ) {

                            val trackerUrl =
                                "http://$ip/"


                            copySessionCookieTo(
                                trackerUrl
                            ) {

                                webView.loadUrl(
                                    trackerUrl
                                )
                            }

                        } else {

                            Toast.makeText(
                                this@MainActivity,
                                "Tracker introuvable",
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }

                    return@runOnUiThread
                }


                Toast.makeText(
                    this@MainActivity,
                    "URL tracker non autorisée",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }
        // ============================================================
// VIBRATION ANDROID
// ============================================================

        @JavascriptInterface
        fun vibrate(pattern: String?) {

            try {

                android.util.Log.d(
                    "TRAK_VIBRATION",
                    "Commande vibration reçue : $pattern"
                )

                val values = pattern
                    ?.split(",")
                    ?.mapNotNull { it.trim().toLongOrNull() }
                    ?.toLongArray()
                    ?: longArrayOf(400L)


                if (values.isEmpty()) {
                    return
                }


                if (
                    android.os.Build.VERSION.SDK_INT >=
                    android.os.Build.VERSION_CODES.S
                ) {

                    val vibratorManager =
                        getSystemService(
                            Context.VIBRATOR_MANAGER_SERVICE
                        ) as VibratorManager

                    val vibrator =
                        vibratorManager.defaultVibrator

                    if (!vibrator.hasVibrator()) {
                        android.util.Log.w(
                            "TRAK_VIBRATION",
                            "Téléphone sans vibreur"
                        )
                        return
                    }


                    vibrator.vibrate(
                        VibrationEffect.createWaveform(
                            values,
                            -1
                        )
                    )

                } else {

                    @Suppress("DEPRECATION")
                    val vibrator =
                        getSystemService(
                            Context.VIBRATOR_SERVICE
                        ) as Vibrator

                    @Suppress("DEPRECATION")
                    vibrator.vibrate(
                        VibrationEffect.createWaveform(
                            values,
                            -1
                        )
                    )
                }

            } catch (e: Exception) {

                android.util.Log.e(
                    "TRAK_VIBRATION",
                    "Erreur vibration Android",
                    e
                )
            }
        }
    }


    // ============================================================
    // OSM VIA 4G EN WIFI DIRECT
    // ============================================================

    private fun isDirectMode(): Boolean {
        return preferences.getString(
            PREF_LAST_MODE,
            MODE_LOCAL
        ) == MODE_DIRECT
    }


    private fun isOsmTileUrl(
        uri: Uri
    ): Boolean {

        val host = uri.host?.lowercase()
            ?: return false

        if (
            host != OSM_TILE_HOST &&
            !host.endsWith(".openstreetmap.org")
        ) {
            return false
        }

        val path = uri.path?.lowercase()
            ?: return false

        return path.endsWith(".png") ||
                path.endsWith(".jpg") ||
                path.endsWith(".jpeg") ||
                path.endsWith(".webp")
    }


    private fun requestCellularNetwork() {

        val connectivityManager =
            getSystemService(Context.CONNECTIVITY_SERVICE)
                    as ConnectivityManager

        if (cellularNetworkCallback != null)
            return

        val request =
            NetworkRequest.Builder()
                .addTransportType(
                    NetworkCapabilities.TRANSPORT_CELLULAR
                )
                .addCapability(
                    NetworkCapabilities.NET_CAPABILITY_INTERNET
                )
                .build()

        val callback =
            object : ConnectivityManager.NetworkCallback() {

                override fun onAvailable(
                    network: Network
                ) {

                    cellularNetwork = network

                    android.util.Log.d(
                        "TRAK_NET",
                        "Réseau 4G disponible pour OSM"
                    )
                }

                override fun onLost(
                    network: Network
                ) {

                    if (cellularNetwork == network) {
                        cellularNetwork = null

                        android.util.Log.d(
                            "TRAK_NET",
                            "Réseau 4G OSM perdu"
                        )
                    }
                }
            }

        cellularNetworkCallback = callback

        try {
            connectivityManager.requestNetwork(
                request,
                callback
            )
        } catch (e: Exception) {
            cellularNetworkCallback = null

            android.util.Log.e(
                "TRAK_NET",
                "Impossible de demander le réseau 4G pour OSM",
                e
            )
        }
    }


    private fun releaseCellularNetwork() {

        val connectivityManager =
            getSystemService(Context.CONNECTIVITY_SERVICE)
                    as ConnectivityManager

        cellularNetwork = null

        cellularNetworkCallback?.let { callback ->
            try {
                connectivityManager.unregisterNetworkCallback(
                    callback
                )
            } catch (_: Exception) {
                // Déjà libéré / indisponible.
            }
        }

        cellularNetworkCallback = null
    }


    private fun openExternalThroughCellular(
        network: Network,
        uri: Uri,
        requestHeaders: Map<String, String>
    ): WebResourceResponse? {

        val connection =
            network.openConnection(
                URL(uri.toString())
            ) as HttpURLConnection

        connection.connectTimeout = OSM_TILE_TIMEOUT_MS
        connection.readTimeout = OSM_TILE_TIMEOUT_MS
        connection.instanceFollowRedirects = true
        connection.requestMethod = "GET"
        connection.setRequestProperty(
            "User-Agent",
            requestHeaders["User-Agent"] ?: "TRAK Android WebView"
        )

        // Conserver quelques en-têtes utiles sans laisser passer des
        // en-têtes de connexion propres au Wi-Fi.
        requestHeaders["Accept"]?.let {
            connection.setRequestProperty("Accept", it)
        }
        requestHeaders["Accept-Language"]?.let {
            connection.setRequestProperty("Accept-Language", it)
        }

        connection.connect()

        if (connection.responseCode !in 200..299) {
            connection.disconnect()
            return null
        }

        val contentType =
            connection.contentType
                ?.substringBefore(';')
                ?.trim()
                ?.ifEmpty { "application/octet-stream" }
                ?: "application/octet-stream"

        val encoding = connection.contentEncoding

        return WebResourceResponse(
            contentType,
            encoding,
            connection.inputStream
        ).also { response ->
            response.responseHeaders = mapOf(
                "Cache-Control" to "public, max-age=86400"
            )
        }
    }


    // ============================================================
    // ANCIENNE API CONSERVÉE
    // ============================================================

    private fun findTracker(
        callback: (String?) -> Unit
    ) {

        if (
            scanRunning.getAndSet(true)
        ) {

            return
        }


        findTrackerInternal { ip ->

            scanRunning.set(
                false
            )

            callback(
                ip
            )
        }
    }


    // ============================================================
    // CONFIGURATION
    // ============================================================

    override fun onConfigurationChanged(
        newConfig: Configuration
    ) {

        super.onConfigurationChanged(
            newConfig
        )

        /*
         * Pas de reload de la WebView.
         */
    }


    // ============================================================
    // DESTROY
    // ============================================================

    override fun onDestroy() {

        scanRunning.set(
            false
        )

        scanExecutor.shutdownNow()

        releaseCellularNetwork()

        super.onDestroy()
    }
}