# Development Notes — SpiritAshCompanion Reforged

## Цель проекта

Постоянный Spirit Ash-компаньон для Elden Ring Reforged без редактирования `regulation.bin` на диске. Мод должен:

- автоматически призывать выбранного духа;
- переживать переходы и смерть/исчезновение компаньона;
- применять Reforged Fury;
- не вызывать периодических фризов;
- догонять игрока и проходить за ним через boss fog;
- поддерживать несколько пресетов через INI.

## Текущая стабильная база

**v3.8.5 DIRECT CATCH-UP FALLBACK**.

Её нужно считать контрольной точкой. Любые новые эксперименты сначала сравнивать с ней по трём критериям:

1. нет периодических зависаний;
2. Fury остаётся активной;
3. companion не перепризывается циклически.

## Рабочая архитектура

### 1. Summon pipeline

Используется `SummonBuddyManager` и существующий Spirit Ash pipeline игры.

Основная последовательность:

- дождаться валидных world/player/manager;
- pre-arm менеджер;
- скопировать spawn origin игрока;
- отправить ровно один summon request;
- короткое удержание безопасных manager fields;
- после появления духа выполнить bounded identity acquisition;
- закэшировать `ChrIns` выбранного духа.

Повторный summon request выполняется только при подтверждённой стабильной потере компаньона, а не при кратковременном transition/warp состоянии.

### 2. Identity acquisition

Исторически основным источником лагов был поиск духа в ChrSet.

Нельзя делать:

- `VirtualQuery` на каждой записи ChrSet;
- полный scan каждые 250–500 мс;
- бесконечный поиск, пока дух жив.

Текущая схема:

- bounded поиск только после summon handshake;
- raw/SEH reads;
- при необходимости one-shot `ChrSet::safe_get` fallback;
- после успешного захвата хранится cached `ChrIns`.

Torrent может находиться в том же SummonBuddy ChrSet, поэтому первый non-null entry выбирать нельзя. Исторически встречался `NpcParam=80020000` для Torrent.

### 3. Fury

Рабочая стратегия:

- один раз определить точного духа;
- снять passive wrapper конкретного пресета;
- применить Fury wrapper;
- сделать wrapper постоянным (`effectEndurance = -1`);
- Reforged Fury core: `299030`;
- passive core: `299020`;
- cached rearm примерно каждые 22 секунды;
- rearm работает только по cached `ChrIns`;
- никаких healthy-state ChrSet/SpEffect scans.

Это принципиально: старые версии с частым поиском SpecialEffect/ChrSet вызывали фризы.

### 4. Transition safety

После изменения world/player/manager используется quiet window около 4 секунд.

Death/transition guard не должен постоянно разыменовывать player ChrIns internals. В v3.7 это было специально убрано после нестабильных death-transition тестов.

### 5. Respawn watchdog

Историческая проблема: `SummonBuddyManager alive byte` иногда кратковременно сообщает отсутствие духа, хотя он существует или находится в warp/transition состоянии.

Поэтому:

- отсутствие должно сохраняться несколько секунд;
- есть cooldown между summon handshake;
- во время catch-up/warp recovery respawn подавляется;
- cached companion может быть более авторитетным источником, чем transient manager alive flag.

### 6. Catch-up / boss fog

Штатный `SummonBuddyWarpManager` используется как первый уровень помощи:

- уменьшены distance/path/blocked-ray thresholds;
- это полезно для обычного отставания.

Но boss fog показал, что штатный warp не всегда успевает создать/обработать нужную запись в Reforged.

Попытки v3.8.2–v3.8.4:

- напрямую ждать warp-entry — запись не появлялась;
- ставить `SummonBuddyGroup::warp_requested` — manager кратковременно менял alive state;
- watchdog начинал перепризывать духа.

Финальное решение v3.8.5:

- transition сам помечает необходимость recovery;
- после восстановления стабильных указателей пытаемся вернуть уже существующего духа;
- через `CSChrPhysicsModule` синхронизируются:
  - `position`;
  - `last_update_position`;
  - `chr_proxy_pos_update_requested`;
- новый summon request не нужен;
- при обычном отставании direct fallback разрешён примерно после 18 м;
- cooldown примерно 6 секунд.

В пользовательском тесте v3.8.5 этот подход подтвердился как рабочий: дух переносится и при этом нет циклического перепризыва.

## Пресеты и известные ID

### Finlay +10

- Goods +10: `223010`;
- базовый summon/Npc family: `223000`;
- Fury wrapper: `223200`;
- passive wrapper: `223100`.

Важно: в одном реальном Reforged запуске после summon fallback захватывался runtime `NpcParam=138001000`, а не ожидаемый vanilla ID. Поэтому текущий код не должен жёстко зависеть только от `223000` для окончательного capture.

### Jarwight Puppet +10

- Goods +10: `263010`;
- summon family/trigger: `263000`;
- runtime NpcParam: `100000060`;
- Fury wrapper: `263200`;
- passive wrapper: `263100`.

### Mimic Tear +10

Сохраняется как отдельный preset. Не смешивать с прямым NPC spawning экспериментами.

### Tarnished / Nepheli Loux Puppet +10

Используется как человеческий melee Spirit Ash preset. Сохранять через обычный summon pipeline, а не через экспериментальный `CSDebugChrCreator`.

## Неудачные направления, которые не стоит повторять без причины

### Постоянный ChrSet scan

Самый явный источник периодических фризов. Особенно плохо работал вариант, где `readable_range()`/`VirtualQuery()` вызывался для каждой записи и каждого кандидата.

### Постоянный SpEffect scan

Даже если работает функционально, слишком дорог для healthy-state loop. Cached pointer + редкий rearm лучше.

### Blind first-entry selection

SummonBuddy ChrSet может содержать Torrent и Spirit Ash одновременно. Нужно проверять идентичность.

### Считать manager alive byte абсолютной истиной

Во время transition/warp он может кратковременно падать. Это приводило к повторным summon handshake.

### Direct NPC spawn как основная архитектура

Эксперименты с `CSDebugChrCreator`, Latenna и другими NPC доказали, что прямой spawn может вернуть логический `ChrIns`, но модель/AI/lifecycle требуют дополнительных force-load/render flags. Для основной постоянной системы Spirit Ash pipeline оказался надёжнее.

## Логирование

Полезные строки стабильной версии:

```text
[IDENTITY] SUCCESS: cached Spirit Ash NpcParam=...
[FURY] ONE-SHOT transition completed successfully.
[FURY] Cached rearm executed successfully; future rearms stay silent to avoid log I/O.
[WARP] BOSS-FOG FALLBACK: existing companion physics synchronized to player; no resummon.
[WARP] DISTANCE FALLBACK: companion was over 18m away; one-shot physics sync completed.
[RESPAWN] Catch-up recovery active; alive=0 is temporarily ignored and no summon request will be sent.
```

Красные флаги в новых версиях:

```text
[SPAWN] Spirit Ash summon request sent.
```

не должен повторяться каждые несколько секунд во время обычного боя.

Также не должно быть повторяющихся bounded identity lookup/ChrSet scans в healthy-state.

## Среда разработки

Проект разрабатывался и тестировался на Elden Ring Reforged ветке 2.3.4.1. В ходе разработки версия самой игры менялась между 1.16.x и 1.17.x, поэтому бинарные сигнатуры при будущих обновлениях игры/Reforged необходимо перепроверять.

Мод не должен редактировать `regulation.bin` на диске.

## Следующие разумные улучшения

- вынести thresholds catch-up (`18m`, cooldown `6s`) в INI;
- добавить отдельное включение/выключение direct boss-fog fallback;
- логировать выбранный preset + actual runtime NpcParam одной компактной строкой;
- при обновлении Reforged первым делом проверять сигнатуры apply/remove SpEffect и layout manager/physics structures;
- рассмотреть GitHub Actions для автоматической сборки DLL, если будет добавлен воспроизводимый Windows toolchain/SDK setup.
