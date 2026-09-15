# Changelog

## v3.8.5 — Direct Catch-Up Fallback

Текущая рабочая версия.

- сохранены четыре пресета: Finlay, Jarwight, Mimic Tear, Tarnished/Nepheli;
- сохранён cached Fury rearm без healthy-state сканов;
- добавлен direct catch-up через `CSChrPhysicsModule`;
- при boss-fog/transition существующий дух синхронизируется с позицией игрока без нового summon request;
- при обычном отставании fallback срабатывает дальше ~18 м;
- cooldown direct catch-up — 6 секунд;
- respawn подавляется на время recovery, чтобы не вернуть цикл перепризыва.

## v3.8.4 — Warp/Respawn Interlock

- устранялся цикл `warp_requested -> alive=0 -> respawn -> transition -> warp_requested`;
- warp делался одноразовым;
- временный `alive=0` во время warp перестал считаться смертью;
- собственного cache refresh после respawn больше недостаточно для boss-fog trigger.

Проблема: на реальном входе к боссу warp-триггер мог не успевать выполниться до временной потери player/manager.

## v3.8.3 — Group Warp Request

- вместо попытки напрямую работать с warp-entry использован `SummonBuddyGroup::warp_requested`;
- выяснилось, что это может кратковременно уронить manager `alive` и конфликтовать с watchdog.

Проблема: появился циклический перепризывающийся дух примерно каждые несколько секунд.

## v3.8.2 — Boss Fog Warp

- попытка форсировать штатный `RequestWarp` после transition;
- лог показал `no live buddy warp entry` — искали warp-entry слишком поздно/не на той стадии.

## v3.8.1 — Native Warp Assist

- уменьшены штатные пороги `SummonBuddyWarpManager`;
- примерно 15–20 м дистанции и ~0.75 с blocked ray/path;
- улучшило обычный catch-up, но само по себе не переносило духа через boss fog.

## v3.8.0 — Four Presets

Добавлены переключаемые пресеты:

- 0 — Cleanrot Knight Finlay +10;
- 1 — Jarwight Puppet +10;
- 2 — Mimic Tear +10;
- 3 — Tarnished / Nepheli Loux Puppet +10.

Сохранена стабильная cached Fury схема.

## v3.7.2 — Finlay Cached Fury Restore

- восстановлена рабочая схема Fury;
- companion identity захватывается один раз;
- далее Fury работает по cached `ChrIns`;
- periodic full ChrSet/SpEffect scans исключены.

## v3.7.1 — Finlay No-Hitch

- убраны per-entry `VirtualQuery` из поиска Finlay;
- прямые чтения защищены SEH;
- устранены заметные зависания каждые несколько секунд;
- проблема: слишком жёсткий identity lookup мог не находить Finlay, поэтому Fury не применялась.

## v3.7 — Transition-Quiet Death Fix

- player ChrIns internals больше не используются death guard'ом;
- после смены world/player/manager — 4 секунды тишины;
- отсутствие компаньона должно быть стабильным несколько секунд до respawn;
- cached Fury rearm ~22 секунды.

## v3.5–v3.6

- переход к cached Fury;
- попытки death guard;
- выявлено, что player ChrIns проверки сами могут быть нестабильны в transition/death состояниях.

## v3.3–v3.4

- отказ от постоянного SpEffect сканирования;
- снижение накладных расходов monitor loop;
- обнаружены ложные `alive=0`, вызывавшие повторные respawn handshake.

## v2.8–v3.2

- сформирована базовая рабочая summon handshake;
- one-shot Fury;
- затем respawn monitor и cached rearm;
- основа дальнейшей стабильной ветки.
