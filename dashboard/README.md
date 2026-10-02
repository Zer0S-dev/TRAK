# TRAK Dashboard

Initial dashboard skeleton: PHP + SQLite authentication.

## Authentication
- If the database contains no users, `public/register.php` is the mandatory first page.
- The first registered user is automatically assigned the `admin` role.
- Once an account exists, registration is restricted to authenticated administrators.
- Passwords are stored with PHP `password_hash()`.
- Sessions use secure cookie settings when HTTPS is detected.

## Structure
- `public/`: web root
- `app/`: application/bootstrap/auth code
- `storage/`: SQLite database (kept outside the public directory)
