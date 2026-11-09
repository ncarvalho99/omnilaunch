# Focused host-only validation. No downloads, secrets, SD writes or deployment.
# Run from product/: powershell -NoProfile -File tests/cards_host_tests.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$script = @'
set -eu
J=$(find /root/.xmake/packages/n/nlohmann_json -path '*/include/nlohmann/json.hpp' | head -1)
test -n "$J"
g++ -std=c++20 -Wall -Wextra -pthread -Iprojects/menu/src -Iprojects/common/include -I"${J%/nlohmann/json.hpp}" -idirafter lib/nxui/include tests/cards_pages_test.cpp projects/menu/src/cards/CardsPages.cpp projects/menu/src/cards/CardsAchievements.cpp projects/menu/src/core/Config.cpp -o /tmp/cards_pages_test
(cd /tmp && ./cards_pages_test)
g++ -std=c++20 -Wall -Wextra -Itests/cards_sheet_stubs -Iprojects/menu/src -idirafter lib/nxui/include tests/cards_sheet_test.cpp projects/menu/src/cards/CardsSheet.cpp -o /tmp/cards_sheet_test
/tmp/cards_sheet_test
g++ -std=c++20 -Wall -Wextra -Iprojects/menu/src tests/lock_password_test.cpp projects/menu/src/widgets/LockPassword.cpp -o /tmp/lock_password_test
/tmp/lock_password_test
g++ -std=c++20 -Iprojects/menu/src -Iprojects/common/include -I"${J%/nlohmann/json.hpp}" -idirafter lib/nxui/include tests/config_cards_layout_test.cpp projects/menu/src/core/Config.cpp -o /tmp/config_cards_test
(cd /tmp && ./config_cards_test)
g++ -std=c++17 -pthread -Itests/home_audio/stubs -Iprojects/menu/src -idirafter lib/nxui/include tests/home_audio/home_audio_test.cpp projects/menu/src/home/HomeAudio.cpp projects/menu/src/home/HomeCore.cpp projects/menu/src/home/HomePlaytime.cpp -o /tmp/home_audio_test
/tmp/home_audio_test
g++ -std=c++17 -Wall -Wextra -Iprojects/menu/src tests/net_test.cpp projects/menu/src/net/NetUtil.cpp projects/menu/src/net/WsFrame.cpp -o /tmp/net_test
/tmp/net_test
'@
& docker run --rm --network none -v "${repo}:/src:ro" -v switchu-xmake:/root/.xmake:ro -w /src --entrypoint sh switchu-build:latest -c $script
if ($LASTEXITCODE -ne 0) { throw "Cards host regressions failed (exit $LASTEXITCODE)." }
