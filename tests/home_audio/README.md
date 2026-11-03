# Home audio host contract tests

Compile the production HomeAudio and HomeCore with the real nxui ThreadPool and
small device fakes (no packages/downloads, SD writes or console deployment):

```powershell
docker run --rm --network none -v "${PWD}:/src:ro" -w /src --entrypoint sh switchu-build:latest -c 'g++ -std=c++17 -pthread -Itests/home_audio/stubs -Iprojects/menu/src -idirafter lib/nxui/include tests/home_audio/home_audio_test.cpp projects/menu/src/home/HomeAudio.cpp projects/menu/src/home/HomeCore.cpp -o /tmp/home_audio_test && /tmp/home_audio_test'
```

The fakes exercise process ownership, playlist idempotence, immutable load
snapshots, two-worker serialization and final shutdown drain/order. They do not
validate real SDL_mixer/espeak, GPU teardown or audible console behaviour.

Use -idirafter for nxui: its Switch endian.h must not shadow glibc's endian.h.
