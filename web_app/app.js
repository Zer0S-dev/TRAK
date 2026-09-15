/* TRAK 3.0.5 — dashboard entry point */

(function () {
    const modules = [
        'js/core.js',
        'js/map.js',
        'js/dashboard.js',
        'js/settings.js',
        'js/wifi.js',
        'js/ui.js'
    ];

    function loadScript(src) {
        return new Promise((resolve, reject) => {
            const script = document.createElement('script');
            script.src = src;
            script.onload = resolve;
            script.onerror = () => reject(new Error('Impossible de charger ' + src));
            document.head.appendChild(script);
        });
    }

    async function boot() {
        try {
            for (const module of modules) await loadScript(module);

            const start = async () => {
                initTrackerMap();
                updateFollowButton();
                try {
                    await loadSession();
                    await refresh();
                    await loadTrackserver();
                    await loadRecordInterval();
                    await loadWifiProfiles();
                } catch (error) {
                    if (error.message !== 'unauthorized') console.warn('[TRAK] Session:', error);
                }
                window.setInterval(refresh, REFRESH_INTERVAL_MS);
                window.setInterval(updateMotionCountdown, 250);
            };

            if (document.readyState === 'loading') {
                document.addEventListener('DOMContentLoaded', start, { once: true });
            } else {
                await start();
            }
        } catch (error) {
            console.error('[TRAK] Dashboard:', error);
        }
    }

    boot();
})();
