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
    return rtrim(app_url(), '/') . '/' . ltrim($path, '/');
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
