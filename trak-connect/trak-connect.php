<?php
/**
 * Plugin Name: TRAK Connect
 * Description: Réception des positions GPS TRAK via API REST et affichage WordPress.
 * Version: 0.2.0
 * Author: TRAK
 */

if (!defined('ABSPATH')) {
    exit;
}

/*
 * ============================================================
 * TRAK CONNECT
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
 * trak_connect_last_position
 *
 * Shortcode :
 *   [trak_connect]
 */


/**
 * Stocke la dernière position reçue.
 */
function trak_connect_store_position($lat, $lon)
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

    return update_option(
        'trak_connect_last_position',
        $data,
        false
    );
}


/**
 * Retourne la dernière position connue.
 */
function trak_connect_get_position()
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
 * REST API : réception d'une position TRAK.
 *
 * POST
 * /wp-json/trak-connect/v1/position
 *
 * JSON :
 * {
 *   "type": "position",
 *   "lat": 49.123456,
 *   "lon": 1.234567
 * }
 */
function trak_connect_receive_position(
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

    /*
     * Le type est obligatoire.
     */
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

    /*
     * Latitude / longitude obligatoires.
     */
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

    /*
     * Vérification géographique.
     */
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

    /*
     * Stockage.
     */
    if (!trak_connect_store_position($lat, $lon)) {
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
            'success' => true,
            'type'    => 'position',
            'lat'     => $lat,
            'lon'     => $lon,
            'received'=> current_time('mysql'),
        ),
        200
    );
}


/**
 * REST API : lecture de la dernière position.
 *
 * GET
 * /wp-json/trak-connect/v1/position
 */
function trak_connect_get_position_api()
{
    $position = trak_connect_get_position();

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
 * Enregistrement des routes REST.
 */
function trak_connect_register_rest_routes()
{
    register_rest_route(
        'trak-connect/v1',
        '/position',
        array(
            /*
             * POST = TRAK -> WordPress
             */
            array(
                'methods'             => WP_REST_Server::CREATABLE,
                'callback'            => 'trak_connect_receive_position',
                'permission_callback' => '__return_true',
            ),

            /*
             * GET = consultation
             */
            array(
                'methods'             => WP_REST_Server::READABLE,
                'callback'            => 'trak_connect_get_position_api',
                'permission_callback' => '__return_true',
            ),
        )
    );
}

add_action(
    'rest_api_init',
    'trak_connect_register_rest_routes'
);


/**
 * Shortcode [trak_connect]
 */
function trak_connect_shortcode()
{
    $position = trak_connect_get_position();

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

        <?php if (!$position): ?>

            <p>
                <strong>TRAK :</strong>
                aucune position reçue.
            </p>

        <?php else: ?>

            <p>
                <strong>Latitude :</strong>
                <?php
                echo esc_html(
                    number_format(
                        (float) $position['lat'],
                        6,
                        '.',
                        ''
                    )
                );
                ?>
            </p>

            <p>
                <strong>Longitude :</strong>
                <?php
                echo esc_html(
                    number_format(
                        (float) $position['lon'],
                        6,
                        '.',
                        ''
                    )
                );
                ?>
            </p>

            <p>
                <strong>Dernière réception :</strong>
                <?php
                echo esc_html(
                    $position['received'] ?? ''
                );
                ?>
            </p>

        <?php endif; ?>

    </div>

    <?php

    return ob_get_clean();
}

add_shortcode(
    'trak_connect',
    'trak_connect_shortcode'
);