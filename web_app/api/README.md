# TRAK Connect API — SQLite

TRAK Connect 3.2.0 no longer stores TRAK positions or settings in public JSON files.

## Database

The API uses PHP PDO with SQLite. `PDO_SQLITE` must be available on the server. urlPHP PDO SQLite documentationhttps://www.php.net/manual/en/ref.pdo-sqlite.php

The database file must be stored **outside the public web root**.

Recommended production setup:

```text
/private/trak.sqlite        # private, not directly reachable by HTTP
/public/trak/               # web_app contents
```

Set the absolute path with the environment variable:

```text
TRAK_CONNECT_DB=/private/trak.sqlite
```

If the variable is not set, the code uses its local default path `../../private/trak.sqlite` relative to `web_app/api/`. Verify this path is outside the document root on the target hosting.

## Tables

- `devices`: one row per TRAK.
- `positions`: historical GPS/telemetry positions.
- `device_settings`: server-side configuration, including Wi-Fi profiles and configuration version.

The schema is also documented in `sqlite_schema.sql` and is created automatically by `config.php` on first use.

## Legacy JSON migration

If an existing installation still has:

- `api/storage/positions.json`
- `api/storage/settings.json`

`config.php` imports them once into SQLite and removes the legacy files after a successful migration.

The files are no longer tracked by the 3.2.0 branch.

## Security

- No sensitive position/settings JSON file is used as persistent storage.
- SQLite is accessed only by PHP.
- Dashboard authentication remains handled by the existing PHP session layer.
- SQL writes use PDO prepared statements.
- API-key authentication is the next TRAK Connect migration step; it is intentionally not mixed into this storage migration yet.
