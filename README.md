# 5 Hundred

Jeu de cartes **5 Hundred** multijoueur en réseau.

- **Client** : C11 + SDL2, compilable nativement (macOS / Linux / Windows) et en **WebAssembly** via Emscripten.
- **Serveur** : Go — autorité sur les règles, WebSocket, signalisation WebRTC.
- **Variantes** : 4 joueurs (2 équipes) et 2 joueurs (avec tableaux face cachée).
- **Fonctionnalités** : chat texte, chat vocal et webcam (WebRTC).

## Prérequis

### Client natif
- CMake ≥ 3.20
- SDL2, SDL2_image, SDL2_ttf, SDL2_mixer
- **libwebsockets** ≥ 4.x (transport WebSocket natif)
- **cJSON** (embarqué automatiquement via CMake FetchContent — aucune installation requise)

```bash
# macOS
brew install cmake sdl2 sdl2_image sdl2_ttf sdl2_mixer libwebsockets
```

```bash
# Ubuntu / Debian
apt install cmake libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
            libsdl2-mixer-dev libwebsockets-dev
```

### Client WASM
- [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
- `source <emsdk_dir>/emsdk_env.sh`
- SDL2, cJSON et WebSocket sont gérés nativement par Emscripten (pas d'installation supplémentaire)

### Serveur
- Go ≥ 1.22
- Les dépendances Go sont téléchargées automatiquement par `go mod download` :
  - `nhooyr.io/websocket` v1.8.17 — WebSocket pur Go (fonctionne natif et WASM)

## Build & lancement

```bash
# Client natif
./scripts/build-native.sh

# Client WebAssembly
./scripts/build-wasm.sh

# Serveur (compile puis démarre sur :8080)
./scripts/run-server.sh

# Changer le port
PORT=9000 ./scripts/run-server.sh
```

Le serveur expose :
- `GET /health` — état JSON `{"status":"ok","clients":N}`
- `GET /ws` — endpoint WebSocket (protocole `five-hundred`)

## Plan de réalisation

Voir [PLAN.md](PLAN.md).
