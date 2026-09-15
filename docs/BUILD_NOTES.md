# Build notes

Текущая стабильная кодовая база — v3.8.5 DIRECT CATCH-UP FALLBACK.

## Исходник

Главный файл: `src/SpiritAshCompanion_Reforged.cpp`.

Из-за ограничения GitHub-коннектора большой исходник загружен как один translation unit через `#include` файлов `src/parts/part_00.inc` … `part_09.inc`. Суммарный размер этих частей — **110742 байта**, ровно как у исходного локального `SpiritAshCompanion_Reforged.cpp` v3.8.5.

## Toolchain

Рабочие сборки проекта делались как Windows x64 DLL через LLVM/Clang под MSVC ABI и `lld-link`, без зависимости от CRT:

- target: `x86_64-pc-windows-msvc`;
- оптимизация: `-O2`;
- без C++ exceptions/RTTI;
- MS extensions включены;
- точка входа DLL экспортируется как `DllMain`.

Проект сам объявляет необходимые WinAPI imports и минимальные `memcpy`/`memset`, поэтому архитектура исходника намеренно самодостаточная.

## Проверенная среда

- Elden Ring: ветка 1.17.x на финальном этапе разработки;
- Elden Ring Reforged: 2.3.4.1;
- x64 Windows.

После обновлений игры/Reforged необходимо перепроверять сигнатуры `apply_speffect` / `remove_speffect`, WorldChrMan и layout менеджеров/physics structures.

## Контрольные SHA-256 локальной стабильной сборки v3.8.5

```text
4c6982272d5f92921fdb458c4260cf472583930e03fff240fb0fee7a5dcce64c  SpiritAshCompanion_Reforged.dll
1dc347ecb200f52a5590876224ec58e8ddb62d2ef9999fdd489601d65ef94fe9  SpiritAshCompanion_Reforged_v3.8.5_DIRECT_CATCHUP.zip
4f2838ab3f6a62a607012821710544fa0480ba0ac7dc03dd8b5041e859b2592e  original SpiritAshCompanion_Reforged.cpp
```

## Важное правило

Не возвращать постоянные ChrSet/SpEffect scans или per-entry `VirtualQuery` в healthy-state loop. Это уже было подтвержденным источником периодических фризов.
