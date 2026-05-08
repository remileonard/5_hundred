# Plan de réalisation — 5 Hundred en réseau

Ce document décrit le plan progressif de développement d'un jeu de cartes **5 Hundred** multijoueur en réseau, avec client natif (C/SDL2) compilable aussi en WebAssembly, serveur en Go, et fonctionnalités de chat vocal + webcam.

---

## 1. Vision générale

- **Jeu** : 500, deux variantes supportées :
  - **4 joueurs** (classique) — 2 équipes de 2, paquet de 43 cartes (joker inclus).
  - **2 joueurs** (head-to-head) — même paquet de 43 cartes ; chaque joueur reçoit **deux mains** : une main principale (10 cartes) + une main "tableau" de 10 cartes posée devant lui (5 visibles sur 5 cartes face cachée).
- **Architecture** : client / serveur centralisé.
- **Client** : C + SDL2 (+ SDL2_image, SDL2_ttf, SDL2_mixer). Compilation native (Linux/macOS/Windows) et WebAssembly via Emscripten.
- **Serveur** : Go (un seul binaire qui gère matchmaking, salons, état du jeu, relais média).
- **Réseau** :
  - Logique de jeu : WebSocket (texte/JSON ou binaire) — fonctionne en natif et WASM.
  - Webcam / voix : WebRTC (relai SFU côté serveur, ou peer-to-peer via signalisation Go).
- **Build** : CMake pour le client (toolchain Emscripten pour la cible WASM). `go build` pour le serveur. Un `Makefile` ou script racine orchestrera les deux.

---

## 2. Stack technique détaillée

### Client C/SDL2
- C11.
- SDL2, SDL2_image, SDL2_ttf, SDL2_mixer.
- Bibliothèque WebSocket : `libwebsockets` (natif) — en WASM, utiliser l'API WebSocket d'Emscripten directement, abstraite derrière une interface unique.
- JSON : `cJSON` (léger, portable WASM).
- Build : CMake ≥ 3.20.

### Serveur Go
- Go ≥ 1.22.
- WebSocket : `github.com/coder/websocket` ou `nhooyr.io/websocket`.
- Signalisation WebRTC : `github.com/pion/webrtc/v3` (option SFU si on veut éviter le P2P).
- Persistance initiale : en mémoire (struct + mutex). Plus tard SQLite/Postgres si besoin.

### Média (voix/vidéo)
- WebRTC.
- En WASM : API navigateur native.
- En natif : `libdatachannel` ou intégration `pion` côté serveur + relai (plus simple : SFU côté serveur, le client natif utilise une bibliothèque WebRTC C/C++ comme `libdatachannel`).
- Décision à confirmer en phase 6.

---

## 3. Règles du 500 à implémenter

### Variante 4 joueurs (par défaut)
- 4 joueurs, 2 équipes (partenaires opposés).
- Paquet de 43 cartes : 4-A pour ♠♣ (10 cartes), 5-A pour ♦♥ (10 cartes), Joker.
- Distribution : 10 cartes par joueur + 3 au "kitty" (chien).
- Enchères : passe / 6♠ ... 10NT (préséance ♠ < ♣ < ♦ < ♥ < NT), misère, open misère.
- Preneur récupère le kitty, écarte 3 cartes, choisit l'atout.
- 10 plis joués. Score selon table officielle.
- Victoire : 500 points. Défaite : -500.

### Variante 2 joueurs (head-to-head)
- 2 joueurs, pas d'équipe.
- **Même paquet de 43 cartes** que la variante 4J.
- Chaque joueur reçoit **deux jeux** :
  - une **main principale** (10 cartes, en main, cachée à l'adversaire),
  - un **tableau** de 10 cartes posées devant lui : 5 cartes face visible (accessibles aux deux joueurs comme info publique) + 5 cartes face cachée en dessous (révélées une à une quand la carte visible au-dessus est jouée).
- Kitty de 3 cartes (comme en 4J), pris par le preneur qui écarte 3 cartes.
- Distribution totale : 2×(10 + 10) + 3 = 43 cartes ✔.
- Enchères identiques à la variante 4J.
- À son tour, le joueur peut jouer **une carte de sa main** ou **une carte visible de son tableau**. Quand une carte visible est jouée, la carte cachée juste en dessous est retournée et devient jouable.
- 20 plis joués (chaque joueur a 20 cartes à jouer au total).
- Score : seuils 500 / -500 conservés (table de score à ajuster pour 20 plis — à valider).

> Le moteur de règles côté serveur sera paramétré par un `Variant` (`Four` / `Two`) qui contrôle la composition du paquet, la distribution (mains + tableau), la taille du kitty, le nombre de plis et la liste des enchères valides.

---

## 4. Phases de développement

### Phase 0 — Fondations du dépôt ✅
- [x] Structure de dossiers (`client/`, `server/`, `shared/`, `assets/`, `cmake/`, `docs/`).
- [x] CMake racine + sous-projet client.
- [x] `go.mod` pour le serveur.
- [x] `.gitignore`, `.editorconfig`, README mis à jour.
- [x] Script `scripts/build-native.sh` et `scripts/build-wasm.sh`.

### Phase 1 — Moteur de jeu (logique pure, sans réseau) ✅
- [x] Représentation des cartes, paquet, mélange déterministe (seed).
- [x] Paramétrage **Variant** (4 joueurs / 2 joueurs) : composition du paquet, taille du kitty, nombre de joueurs.
- [x] Distribution + kitty (selon variante).
- [x] Machine à états : Bidding → Kitty → Playing → Scoring → End.
- [x] Validation des coups (suivre la couleur, atout, joker).
- [x] Calcul du score 500 (commun aux deux variantes).
- [x] **Tests unitaires** couvrant les deux variantes (27 tests, tous verts).
- [x] Le moteur doit être réutilisable côté serveur (réécriture en Go) **ou** compilé en lib partagée. Choix : **réécriture en Go côté serveur** (le serveur est l'autorité), le C ne fait que la présentation.

> Décision : le moteur de règles vit dans `server/game/` en Go. Le client C contient un moteur "miroir" simplifié pour l'animation et la prédiction locale, mais ne valide rien.

### Phase 2 — Squelette client SDL2 ✅
- [x] Fenêtre, boucle d'événements, rendu de base.
- [x] Chargement des sprites de cartes (rendu procédural — rectangles + texte SDL_ttf).
- [x] Affichage de la main du joueur, du tapis, des autres joueurs.
- [x] Système d'écran (menu → lobby → table).
- [x] UI minimale (boutons, texte) — pas de framework, dessin manuel (`ui/ui.c`).
- [x] Build natif macOS tourne (WASM : infrastructure prête, police à copier dans assets/).

### Phase 3 — Serveur Go minimal ✅
- [x] HTTP + WebSocket endpoint `/ws`.
- [x] Protocole JSON : `{"type": "...", "payload": {...}}` (`internal/protocol/msg.go`).
- [x] Gestion des connexions, ping/pong (`internal/ws/hub.go`).
- [x] Salons (rooms) : créer / rejoindre / lister (`internal/room/manager.go`).
- [x] Sérialisation de l'état de jeu (`internal/room/serial.go`).
- [x] Routeur de messages (`internal/handler/handler.go`).

### Phase 4 — Intégration réseau client ✅
- [x] Couche transport abstraite : `net_send/net_recv` avec deux impls (`net_native.c` libwebsockets, `net_wasm.c` Emscripten).
- [x] Connexion au serveur, identification (auto-connect + `identify` → `welcome`).
- [x] Lobby visuel : liste de salons, créer 4j/2j, rejoindre, rafraîchir.
- [x] Synchronisation de l'état de la partie reçu du serveur (`game.state` → `ClientGameState`).

### Phase 5 — Boucle de jeu complète en réseau ✅
- [x] Serveur : bots de remplissage (`ws.NewBotClient`, `room.RunBot`).
- [x] Serveur : message `room.start` → `StartWithBots` (lance bots + partie).
- [x] Serveur : broadcast `game.event{event:"game_over"}` en fin de partie.
- [x] Client : `net_send_bid`, `net_send_discard`, `net_send_play`, `net_send_room_start`.
- [x] Client : parsing du champ `kitty` dans `game.state` → `ClientGameState.kitty[]`.
- [x] Client : UI enchères (grille 6♠–10NT + Misère + Open Misère + Passer, bids invalides grisés).
- [x] Client : UI défausse (main + kitty affichées, sélection bitmask 3 cartes, bouton Défausser).
- [x] Client : UI pose de carte (sélection + bouton Jouer ▶ sur son tour).
- [x] Client : UI fin de partie (overlay scores + bouton Nouvelle partie).
- [x] Client : bouton "Démarrer avec bots" dans le panneau d'attente.
- [x] Mode 2 joueurs jouable de bout en bout (mains principales + tableaux 5 visibles / 5 cachées, paquet 43).
- [x] Sélection de la variante à la création du salon.
- [x] Pose de cartes, animations.

### Phase 6 — Voix et webcam
- [ ] Intégration WebRTC.
- [ ] Signalisation via le WebSocket déjà en place.
- [ ] SFU côté serveur (Pion).
- [ ] Affichage des flux vidéo dans les "places" autour de la table.
- [ ] Mute / camera off / push-to-talk.

### Phase 7 — Chat texte
- [ ] Canal de chat par salon (transport WebSocket existant).
- [ ] UI d'affichage + saisie.

### Phase 8 — Polish
- [ ] Sons, animations, effets.
- [ ] Gestion des déconnexions / reconnexions.
- [ ] Bots IA simples pour combler les places vides.
- [ ] Persistance des comptes / classement (optionnel).
- [ ] CI : build natif + WASM + serveur.
- [ ] Packaging : AppImage / .app / .exe / page web statique pour le WASM.

---

## 5. Structure de dossiers cible

```
5_hundred/
├── CMakeLists.txt                # racine, oriente vers client/
├── PLAN.md
├── README.md
├── client/
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c
│   │   ├── render/
│   │   ├── ui/
│   │   ├── net/                  # abstraction WS native + WASM
│   │   ├── game/                 # miroir client de l'état
│   │   └── media/                # WebRTC
│   └── assets/                   # cartes, polices, sons
├── server/
│   ├── go.mod
│   ├── cmd/server/main.go
│   ├── internal/
│   │   ├── game/                 # règles 500 (autorité)
│   │   ├── room/
│   │   ├── ws/
│   │   └── rtc/                  # signalisation / SFU
├── shared/
│   └── protocol.md               # spécification messages JSON
├── cmake/
│   └── emscripten.toolchain.cmake
├── scripts/
│   ├── build-native.sh
│   └── build-wasm.sh
└── docs/
    └── rules-500.md
```

---

## 6. Protocole réseau (premier jet)

Messages client → serveur :
- `hello` `{name}`
- `room.create` / `room.join` `{roomId, variant: "4p"|"2p"}` / `room.list`
- `bid` `{value}` (ex: `"7H"`, `"pass"`, `"misere"`)
- `kitty.discard` `{cards: [3]}`
- `play` `{card}`
- `chat` `{text}`
- `rtc.signal` `{sdp|ice}`

Messages serveur → client :
- `welcome` `{playerId}`
- `room.state` `{players, phase, ...}`
- `hand` `{cards}` (privé)
- `bid.update`, `kitty.reveal`, `trick.update`, `score.update`
- `error` `{code, msg}`
- `chat`, `rtc.signal`

À détailler dans `shared/protocol.md` en phase 3.

---

## 7. Risques & décisions ouvertes

- **WebRTC en C natif** : `libdatachannel` est l'option la plus propre, mais ajoute une dépendance C++. Alternative : ne fournir voix/webcam qu'en build WASM dans un premier temps.
- **Moteur dupliqué C/Go** : on choisit de garder l'autorité côté Go pour éviter la triche ; le client n'a besoin que d'un moteur d'affichage.
- **Sérialisation** : JSON pour démarrer (debug facile), passage à un format binaire (MessagePack / protobuf) si besoin de perf.
- **Authentification** : non prioritaire ; pseudonyme libre au début.

---

## 8. Prochaine étape

Commencer par la **Phase 0** : poser la structure du dépôt, le CMake racine, le `go.mod`, et un "hello SDL" qui se lance en natif.
