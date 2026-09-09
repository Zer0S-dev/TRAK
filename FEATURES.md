TRAK box
- envoi sa position par API à TRAK Connect
- lit l'état qu'il doit adopter (intervals, sentinel, ...)
- écrit son état (intervals, sentinel, stats, reglages, ...)
- utilise en priorité le wifi avec fallback 4G en HTTPS uniquement
- API avec key
- buffer sur SD (model osmand)
- dev log sur SD horodaté

Dashboard TRAK Connect
- REST API / json
- reçois les datas par API de TRAK
- donne les reglages au TRAK
- genere la key API
- communique via PAI avec TRAK
- est une web app
- sert de relais pour envoyer a Trackserver la postion sous forme d'url osmand

Priorités
- performance d'envoi rapide en 4G https
- connexion stable
- rapidité de communication

