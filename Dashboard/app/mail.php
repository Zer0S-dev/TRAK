<?php
declare(strict_types=1);
require_once __DIR__ . '/config.php';

function send_mail(string $to, string $subject, string $body): bool {
    if (!filter_var($to, FILTER_VALIDATE_EMAIL)) return false;

    $from = mail_from();

    $headers = [
        'From: ' . MAIL_FROM_NAME . ' <' . $from . '>',
        'Reply-To: ' . $from,
        'MIME-Version: 1.0',
        'Content-Type: text/plain; charset=UTF-8',
        'X-Mailer: TRAK Connect',
    ];

    return mail($to, $subject, $body, implode("\r\n", $headers));
}

function app_mail_url(string $path): string {
    // Utilise l'URL réelle d'installation du Dashboard, sans chemin codé en dur.
    $baseUrl = app_url();
    return rtrim($baseUrl, '/') . '/' . ltrim($path, '/');
}

function send_email_verification(string $email, string $token): bool {
    $url = app_mail_url('verify-email.php?token=' . rawurlencode($token));
    $body = "Bonjour,\n\nUne adresse email a été ajoutée ou modifiée pour votre compte TRAK Connect.\n\nConfirmez-la ici :\n" . $url . "\n\nCe lien est valable 24 heures.\n\nTRAK Connect";
    return send_mail($email, 'Confirmation de votre adresse email — TRAK Connect', $body);
}

function send_password_reset(string $email, string $token): bool {
    $url = app_mail_url('reset-password.php?token=' . rawurlencode($token));
    $body = "Bonjour,\n\nUne demande de récupération du mot de passe TRAK Connect a été effectuée.\n\nChoisissez un nouveau mot de passe ici :\n" . $url . "\n\nCe lien est valable 1 heure et une seule utilisation.\n\nSi vous n'êtes pas à l'origine de cette demande, ignorez ce message.\n\nTRAK Connect";
    return send_mail($email, 'Récupération du mot de passe — TRAK Connect', $body);
}

function send_user_created_email(string $email, string $username): bool {
    $body = "Bonjour,\n\nVotre compte TRAK Connect a été créé.\n\nIdentifiant : " . $username . "\n\nSi vous n'êtes pas à l'origine de cette création, contactez l'administrateur du Dashboard.\n\nTRAK Connect";
    return send_mail($email, 'Votre compte TRAK Connect a été créé', $body);
}

function send_user_deleted_email(string $email, string $username): bool {
    $body = "Bonjour,\n\nVotre compte TRAK Connect a été supprimé du Dashboard.\n\nIdentifiant : " . $username . "\n\nVous ne pourrez plus utiliser ce compte pour accéder au Dashboard.\n\nTRAK Connect";
    return send_mail($email, 'Votre compte TRAK Connect a été supprimé', $body);
}

function send_trak_created_email(string $email, string $username, string $trakId): bool {
    $body = "Bonjour " . $username . ",\n\nUne nouvelle TRAK Box a été ajoutée à votre compte TRAK Connect.\n\nTRAK ID : " . $trakId . "\n\nLa configuration de cette TRAK est actuellement en attente d'envoi.\n\nTRAK Connect";
    return send_mail($email, 'Nouvelle TRAK Box ajoutée — ' . $trakId, $body);
}

function send_trak_deleted_email(string $email, string $username, string $trakId): bool {
    $body = "Bonjour " . $username . ",\n\nLa TRAK Box suivante a été supprimée de votre compte TRAK Connect :\n\nTRAK ID : " . $trakId . "\n\nLa TRAK n'est plus enregistrée dans le Dashboard.\n\nTRAK Connect";
    return send_mail($email, 'TRAK Box supprimée — ' . $trakId, $body);
}
