# TRAK v20 — firmware 2.0.2wb

## Arborescence

```text
├── tracker_1_7w.ino          <-- Fichier principal (setup / tâches)
├── Config.h                 <-- Pins, constantes et paramètres
├── RuntimeConfig.h          <-- Configuration réseau / NVS
├── RuntimeConfig.cpp        <-- Profils Wi-Fi, Trackserver et compteur data
├── DisplayManager.h         <-- Interface écran
├── DisplayManager.cpp       <-- OLED / U8g2
├── ModemManager.h           <-- Interface modem / GNSS
├── ModemManager.cpp         <-- AT, GNSS, Auto-APN, HTTP
├── MotionManager.h          <-- Détection mouvement
├── MotionManager.cpp        <-- ADXL337 / MOBILE / STATIONNAIRE
├── WebInterface.h            <-- Interface serveur Web
├── WebInterface.cpp          <-- Serveur Web + API REST
├── PositionBuffer.h          <-- Interface buffer FIFO SD
├── PositionBuffer.cpp        <-- Buffer persistant des positions
├── data/
│   └── index.html            <-- Dashboard LittleFS
├── Secrets.h                 <-- Identifiants Wi-Fi (privé)
└── README.md                 <-- Documentation du projet
```

## v20 / firmware 2.0.2wb

- URL Trackserver configurable depuis le Dashboard.
- URL conservée dans la NVS de l'ESP32.
- Même destination utilisée en Wi-Fi et en 4G.
- Le hostname et le chemin sont analysés une seule fois lors du chargement/enregistrement puis conservés en RAM.
- Les envois utilisent des buffers fixes ; aucune construction de `String` n'est ajoutée dans la boucle de transmission.
- La session HTTP 4G persistante est conservée.
- Version firmware fournie à l'interface Web via `/api/data`.
- Compteur mensuel des données envoyées : Wi-Fi, 4G et total.
- Suivi du mois courant basé sur la date GNSS.
- Estimation de consommation 4G avec coefficient opérateur configurable (1.20 par défaut).
- Forfait 4G configurable depuis le Dashboard (150 Mo par défaut).
- Les compteurs sont conservés en NVS avec écritures regroupées pour limiter l'usure de la flash.
- Le compteur mesure le trafic HTTP sortant généré par le tracker ; pour la 4G, les en-têtes ajoutés par la pile HTTP du A7670 ne sont pas directement exposés, d'où le coefficient d'estimation opérateur.
- Version Dashboard reste une valeur statique dans `data/index.html` (1.1).
- Buffer FIFO des positions sur carte SD lorsque le réseau est indisponible.
- Le buffer est persistant après redémarrage de la TRAK.
- Les positions sont envoyées dans l'ordre de capture, une par une, dès que Wi-Fi ou 4G revient.
- Le buffer utilise le fichier `/trak_buffer.bin` sur la SD.
- Capacité du buffer : 8192 positions.
- La carte SD utilise VSPI : SCK GPIO18, MISO GPIO19, MOSI GPIO23, CS GPIO13.
- Deux copies du header du buffer et un CRC permettent de récupérer l'état du buffer après une coupure d'alimentation.

### URL Trackserver

Valeur par défaut :

```text
http://surlereservoir.fr/trackserver/surledoud/eb5a96a7/
```

Le Dashboard accepte une URL `http://hote/chemin` sur le port 80. Les ports explicites et HTTPS ne sont pas supportés dans cette version, afin de conserver le chemin réseau actuel et son optimisation.

## Historique

- 1.0  - Start
- 1.1  - Détection de mouvement / veille
- 1.2  - Optimisation du cœur
- 1.3w - Interface Web
- 1.5w - 3 réseaux Wi-Fi, adresse locale, Wi-Fi direct
- 1.6w - v12 : URL Trackserver configurable depuis le Dashboard
- 1.7w - v13 : compteur mensuel data Wi-Fi / 4G + estimation opérateur
- 1.8w - v14 : authentification Web + login.html
- 1.9w - v19 : application Android + auto-discovery
- 2.0.2wb - v20 : buffer FIFO SD des positions offline

## TODO

1. Sentinelle + alerte SMS
2. Communication SMS TRAK Box ↔ téléphone
3. Affichage sécurisé du numéro SIM sur le Dashboard

## Sécurité

`Secrets.h` contient les identifiants Wi-Fi et ne doit pas être publié.


## Wi-Fi utilisateur — 2.0.2wb

Aucun SSID ou mot de passe Wi-Fi utilisateur n'est embarqué dans le firmware.
La TRAK dispose de 3 profils Wi-Fi enregistrés dans la NVS depuis le Dashboard.

- Profil 1, 2 et 3 sont configurés uniquement par l'utilisateur.
- Si les trois profils sont vides, la TRAK passe directement en 4G.
- Si des profils existent, l'ordre de préférence Wi-Fi/4G et le fallback automatique restent inchangés.
- Le Wi-Fi Direct de configuration reste indépendant des profils Internet.
- Le Dashboard affiche un rappel pour enregistrer un réseau local lorsque les trois profils sont vides.


## 2.0.2wb
Correctif 4G : vérification du PDP/CID 1 et de l’adresse IP avant HTTP, récupération contrôlée de la session HTTP en cas de refus du CID, et logs 4G renforcés. Le buffer SD et la logique Wi-Fi 2.0.1wb restent inchangés.
