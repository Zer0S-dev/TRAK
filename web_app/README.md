# TRAK 3.0 prototype

4G-only TRAK firmware: GNSS position -> REST JSON -> TRAK Connect dashboard -> Trackserver.

Hardware: A7670 RX25 TX26 PWRKEY4 RESET27, WS2812 GPIO12 (centre LED index 0, ring indices 1-6), SD 13/18/19/23, LSM6DS3 I2C 21/22.

## Dashboard authentication

The dashboard is session-protected. The TRAK device endpoint remains protected by the device API key and does not use the dashboard session.

Configure these server environment variables before opening the dashboard:

- `TRAK_CONNECT_USER` — dashboard username.
- `TRAK_CONNECT_PASSWORD_HASH` — password hash generated with PHP `password_hash()`.

Example hash generation:

```bash
php -r 'echo password_hash("CHANGE-ME", PASSWORD_DEFAULT), PHP_EOL;'
```

Do not put the clear-text dashboard password or its hash in the Git repository.

The dashboard uses `/trak/` as its deployment path. The session cookie is `Secure`, `HttpOnly`, and `SameSite=Strict`; therefore the deployed site must use HTTPS.

## API boundaries

- `api/trak/position/` — device-to-server POST, authenticated with `TRAK_API_KEY`.
- `api/session/` — authenticated dashboard session + CSRF token.
- `api/data/` — dashboard session required.
- `api/trackserver/` — dashboard session required; POST additionally requires CSRF.

## Prototype scope

The existing dashboard HTML/CSS is intentionally preserved. Legacy Sentinel/Wi-Fi/SD/motion controls remain visually present but are inert because those functions are not part of the TRAK 3.0 prototype firmware.
