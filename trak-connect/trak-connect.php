<?php
/**
 * Plugin Name: TRAK Connect
 * Description: Réception des positions GPS TRAK via API REST et affichage WordPress avec mise à jour Live.
 * Version: 0.1
 * Author: Zer0S-Dev
 */

if (!defined('ABSPATH')) {
    exit;
}


/*
 * ============================================================
 * TRAK CONNECT 0.2.1
 * ============================================================
 *
 * TRAK
 *   |
 *   | HTTPS POST JSON
 *   v
 * /wp-json/trak-connect/v1/position
 *   |
 *   v
 * WordPress
 *   |
 *   v
 * dernière position
 *
 * Affichage :
 *   [trak_connect]
 *
 * Live :
 *   GET toutes les 1,5 secondes
 */


/**
 * Stockage de la position.
 */
function trak_connect_live_store_position($lat, $lon)
{
    $lat = (float) $lat;
    $lon = (float) $lon;

    if ($lat < -90.0 || $lat > 90.0) {
        return false;
    }

    if ($lon < -180.0 || $lon > 180.0) {
        return false;
    }

    $data = array(
        'lat'       => $lat,
        'lon'       => $lon,
        'timestamp' => current_time('timestamp'),
        'received'  => current_time('mysql'),
    );

    update_option(
        'trak_connect_last_position',
        $data,
        false
    );

    return true;
}


/**
 * Lecture de la dernière position.
 */
function trak_connect_live_get_position()
{
    $data = get_option(
        'trak_connect_last_position',
        false
    );

    if (!is_array($data)) {
        return false;
    }

    return $data;
}


/**
 * POST TRAK -> WordPress.
 */
function trak_connect_live_receive_position(
    WP_REST_Request $request
) {
    $body = $request->get_json_params();

    if (!is_array($body)) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'JSON invalide',
            ),
            400
        );
    }

    if (
        !isset($body['type']) ||
        $body['type'] !== 'position'
    ) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Type de message invalide',
            ),
            400
        );
    }

    if (
        !isset($body['lat']) ||
        !isset($body['lon'])
    ) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Latitude ou longitude manquante',
            ),
            400
        );
    }

    if (
        !is_numeric($body['lat']) ||
        !is_numeric($body['lon'])
    ) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Latitude ou longitude invalide',
            ),
            400
        );
    }

    $lat = (float) $body['lat'];
    $lon = (float) $body['lon'];

    if ($lat < -90.0 || $lat > 90.0) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Latitude hors limites',
            ),
            400
        );
    }

    if ($lon < -180.0 || $lon > 180.0) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Longitude hors limites',
            ),
            400
        );
    }

    if (!trak_connect_live_store_position($lat, $lon)) {
        return new WP_REST_Response(
            array(
                'success' => false,
                'error'   => 'Impossible de stocker la position',
            ),
            500
        );
    }

    return new WP_REST_Response(
        array(
            'success'  => true,
            'type'     => 'position',
            'lat'      => $lat,
            'lon'      => $lon,
            'received' => current_time('mysql'),
        ),
        200
    );
}


/**
 * GET WordPress -> navigateur.
 */
function trak_connect_live_get_position_api()
{
    $position = trak_connect_live_get_position();

    if (!$position) {
        return new WP_REST_Response(
            array(
                'success'  => true,
                'position' => null,
            ),
            200
        );
    }

    return new WP_REST_Response(
        array(
            'success'  => true,
            'position' => $position,
        ),
        200
    );
}


/**
 * Enregistrement de l'API REST.
 */
function trak_connect_live_register_routes()
{
    register_rest_route(
        'trak-connect/v1',
        '/position',
        array(
            array(
                'methods'             => WP_REST_Server::CREATABLE,
                'callback'            => 'trak_connect_live_receive_position',
                'permission_callback' => '__return_true',
            ),

            array(
                'methods'             => WP_REST_Server::READABLE,
                'callback'            => 'trak_connect_live_get_position_api',
                'permission_callback' => '__return_true',
            ),
        )
    );
}

add_action(
    'rest_api_init',
    'trak_connect_live_register_routes'
);


/**
 * Shortcode [trak_connect]
 */
function trak_connect_live_shortcode()
{
    $position = trak_connect_live_get_position();

    ob_start();
    ?>

    <div id="trak-connect"
         style="
            max-width:700px;
            margin:20px auto;
            padding:24px;
            border:1px solid #ddd;
            border-radius:12px;
            font-family:Arial,sans-serif;
         ">

        <h2>TRAK Connect</h2>

        <p>
            <strong>Statut :</strong>
            <span id="trak-connect-status">
                <?php
                echo $position ? 'LIVE' : 'En attente';
                ?>
            </span>
        </p>

        <p>
            <strong>Latitude :</strong>
            <span id="trak-connect-lat">
                <?php
                if ($position) {
                    echo esc_html(
                        number_format(
                            (float) $position['lat'],
                            6,
                            '.',
                            ''
                        )
                    );
                } else {
                    echo '-';
                }
                ?>
            </span>
        </p>

        <p>
            <strong>Longitude :</strong>
            <span id="trak-connect-lon">
                <?php
                if ($position) {
                    echo esc_html(
                        number_format(
                            (float) $position['lon'],
                            6,
                            '.',
                            ''
                        )
                    );
                } else {
                    echo '-';
                }
                ?>
            </span>
        </p>

        <p>
            <strong>Dernière réception :</strong>
            <span id="trak-connect-received">
                <?php
                if (
                    $position &&
                    isset($position['received'])
                ) {
                    echo esc_html(
                        $position['received']
                    );
                } else {
                    echo '-';
                }
                ?>
            </span>
        </p>

    </div>

    <script>
    (function () {

        var endpoint =
            '<?php echo esc_url(rest_url('trak-connect/v1/position')); ?>';

        function updateTrakConnect() {

            var url = endpoint + '?_=' + new Date().getTime();

            fetch(url, {
                method: 'GET',
                cache: 'no-store'
            })
            .then(function (response) {

                if (!response.ok) {
                    throw new Error(
                        'HTTP ' + response.status
                    );
                }

                return response.json();
            })
            .then(function (data) {

                if (
                    !data ||
                    !data.success ||
                    !data.position
                ) {
                    return;
                }

                var position = data.position;

                if (position.lat !== undefined) {
                    document.getElementById(
                        'trak-connect-lat'
                    ).textContent =
                        Number(position.lat).toFixed(6);
                }

                if (position.lon !== undefined) {
                    document.getElementById(
                        'trak-connect-lon'
                    ).textContent =
                        Number(position.lon).toFixed(6);
                }

                if (position.received !== undefined) {
                    document.getElementById(
                        'trak-connect-received'
                    ).textContent =
                        position.received;
                }

                document.getElementById(
                    'trak-connect-status'
                ).textContent = 'LIVE';
            })
            .catch(function () {

                document.getElementById(
                    'trak-connect-status'
                ).textContent = 'OFFLINE';

            });
        }


        /*
         * Première lecture immédiate.
         */
        updateTrakConnect();


        /*
         * Mise à jour toutes les 1,5 secondes.
         */
        setInterval(
            updateTrakConnect,
            1500
        );

    })();
    </script>

    <?php

    return ob_get_clean();
}

add_shortcode(
    'trak_connect',
    'trak_connect_live_shortcode'
);