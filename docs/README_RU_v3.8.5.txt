SpiritAshCompanion Reforged v3.8.5 — DIRECT CATCH-UP FALLBACK

Основа: стабильная ветка v3.8.4/v3.8.0 с 4 пресетами и cached Fury.

Пресеты в SpiritAshCompanion_Reforged.ini:
Preset=0 — Cleanrot Knight Finlay +10
Preset=1 — Jarwight Puppet +10 (метатель горшков)
Preset=2 — Mimic Tear +10
Preset=3 — Tarnished / Nepheli Loux Puppet +10

Что изменено в v3.8.5:
1. Штатные пороги SummonBuddyWarpManager остаются уменьшенными для обычного catch-up.
2. Если при переходе/туманной стене временно пропадают player/manager, мод запоминает необходимость catch-up.
3. После 4 секунд стабильных указателей мод один раз пытается найти уже существующего духа и синхронизирует его CSChrPhysicsModule с текущей позицией игрока.
4. Для синхронизации меняются только position, last_update_position и chr_proxy_pos_update_requested физического модуля. Повторный summon request для этого не нужен.
5. При обычной игре раз в 1 секунду выполняется лёгкая проверка расстояния двух уже известных physics-position. Если дух дальше 18 м, разрешён один direct catch-up; cooldown 6 секунд.
6. Fury остаётся cached: тяжёлого постоянного ChrSet/SpEffect сканирования нет.
7. Respawn временно подавляется во время post-transition catch-up, чтобы не вернуть цикл перепризыва из v3.8.3.

Полезные строки лога:
[WARP] BOSS-FOG FALLBACK: existing companion physics synchronized to player; no resummon.
[WARP] DISTANCE FALLBACK: companion was over 18m away; one-shot physics sync completed.
[RESPAWN] Catch-up recovery active; alive=0 is temporarily ignored and no summon request will be sent.

Для теста туманной стены: войти к боссу, подождать несколько секунд после загрузочного перехода и проверить, появился ли тот же дух рядом с игроком без повторного призыва.
