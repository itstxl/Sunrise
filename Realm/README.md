# Sunrise Realm

Sunrise Realm is the custom-activity-server extension maintained in the Sunrise fork. It keeps the
client mod's offline behavior as the default while moving online services into two headless
processes:

- `sunrise-realm.exe` owns control-plane services such as identity, sessions, matchmaking and host
  allocation.
- `sunrise-activity-host.exe` owns an allocated gameplay instance and its public UDP endpoints.

The current foundation implements a versioned binary control contract, HMAC-SHA256 authenticated
host registration, build fingerprint advertisement, host leases and heartbeats. SignOn, BAP and
gameplay hosting have not been extracted from the injected DLL yet, so this branch is not currently
playable online.

## Build on Linux

The services are Windows console executables during the first implementation phase and run under
Wine or Proton. They use the same Linux-to-Windows toolchain as Sunrise:

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/linux-to-win-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build --target sunrise-realm sunrise-activity-host
```

Portable protocol tests can be built and executed natively without compiling the client DLL:

```bash
cmake -S . -B build-realm-native -G Ninja \
  -DSUNRISE_BUILD_CLIENT=OFF \
  -DSUNRISE_BUILD_REALM=OFF \
  -DSUNRISE_BUILD_TESTS=ON
cmake --build build-realm-native
ctest --test-dir build-realm-native --output-on-failure
```

## Development control connection

Both processes require the same `SUNRISE_REALM_HOST_TOKEN`, containing at least 32 UTF-8 bytes.
The token authenticates every control frame and is never accepted as a command-line argument.
The realm listens only on `127.0.0.1:31090` by default.

Start the realm:

```powershell
$env:SUNRISE_REALM_HOST_TOKEN = "replace-with-a-random-development-secret"
.\sunrise-realm.exe
```

Then register one activity host using the generated SDK catalogue from the operator's installation:

```powershell
$env:SUNRISE_REALM_HOST_TOKEN = "replace-with-a-random-development-secret"
.\sunrise-activity-host.exe `
  --content-catalog "Z:\path\to\Destiny 2\bin\x64\Sunrise\sdk\catalog.bin"
```

`--build-fingerprint` accepts a non-zero 64-digit value for protocol tests that deliberately do not
open a game-derived artifact. It should not be used for a deployed host.

Use `--control-bind`, `--control-port`, `--realm-address`, and `--advertise-address` for an isolated
private network. The control port must not be exposed directly to the public internet.

## Compatibility and content policy

- Realm mode remains opt-in; these executables do not edit a game installation, Wine prefix,
  Sunrise settings or player data.
- All activity/content metadata must be exported at runtime from the operator's supported local
  installation. Packages, extracted assets, generated copyrighted data and game keys must not be
  committed or distributed.
- `RealmId` and build fingerprints are present from protocol version 1 so a second realm or
  incompatible client cannot be admitted accidentally.
