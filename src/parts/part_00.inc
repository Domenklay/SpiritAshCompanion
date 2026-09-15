// SpiritAshCompanion_Reforged.cpp
// v3.8.4 FOUR PRESETS + WARP/RESPAWN INTERLOCK. Stable v3.7.2 no-hitch capture + cached Fury rearm.
// Presets: Finlay / Jarwight / Mimic Tear / Tarnished (Nepheli Loux).
// Healthy gameplay performs no ChrSet/SpEffect scan, no VirtualQuery, and no world/player/manager validation loop.
// Runtime-only; never edits regulation.bin on disk.
// Uses the proven SummonBuddyManager route and fixes a critical identity bug: SummonBuddy ChrSet also contains Torrent,
// so the first non-null ChrIns is NOT necessarily the active Spirit Ash. v2.4 enumerates the set and selects the exact
// the selected Spirit Ash NpcParam before touching Fury. Default is Finger Maiden Therolina +10 (NpcParam 100000040).
// regulation.bin on disk is never edited.

extern "C" {
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef unsigned long DWORD;
typedef unsigned long long ULONGLONG;
typedef unsigned long long SIZE_T;
typedef int BOOL;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef const wchar_t* LPCWSTR;
typedef wchar_t* LPWSTR;
typedef unsigned int UINT;
typedef long long LONGLONG;
typedef short SHORT;
typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE)(LPVOID);

struct MEMORY_BASIC_INFORMATION_X64 {
    LPVOID BaseAddress;
    LPVOID AllocationBase;
    DWORD AllocationProtect;
    WORD PartitionId;
    SIZE_T RegionSize;
    DWORD State;
    DWORD Protect;
    DWORD Type;
};

__declspec(dllimport) HMODULE __stdcall GetModuleHandleW(LPCWSTR);
__declspec(dllimport) BOOL __stdcall DisableThreadLibraryCalls(HMODULE);
__declspec(dllimport) HANDLE __stdcall CreateThread(LPVOID, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD*);
__declspec(dllimport) void __stdcall Sleep(DWORD);
__declspec(dllimport) ULONGLONG __stdcall GetTickCount64();
__declspec(dllimport) DWORD __stdcall GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
__declspec(dllimport) UINT __stdcall GetPrivateProfileIntW(LPCWSTR, LPCWSTR, int, LPCWSTR);
__declspec(dllimport) HANDLE __stdcall CreateFileW(LPCWSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, LPCVOID, DWORD, DWORD*, LPVOID);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);
__declspec(dllimport) SIZE_T __stdcall VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION_X64*, SIZE_T);
__declspec(dllimport) LPVOID __stdcall VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
__declspec(dllimport) BOOL __stdcall VirtualProtect(LPVOID, SIZE_T, DWORD, DWORD*);
__declspec(dllimport) BOOL __stdcall FlushInstructionCache(HANDLE, LPCVOID, SIZE_T);
__declspec(dllimport) HANDLE __stdcall GetCurrentProcess();
__declspec(dllimport) int __stdcall MessageBoxW(void*, LPCWSTR, LPCWSTR, UINT);
__declspec(dllimport) SHORT __stdcall GetAsyncKeyState(int);
}

#define TRUE 1
#define FALSE 0
#define DLL_PROCESS_ATTACH 1
#define INVALID_HANDLE_VALUE ((HANDLE)(LONGLONG)-1)
#define FILE_APPEND_DATA 0x00000004u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define OPEN_ALWAYS 4u
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_FREE 0x00010000u
#define PAGE_EXECUTE_READWRITE 0x40u
#define PAGE_NOACCESS 0x01u
#define PAGE_GUARD 0x100u
#define MB_OK 0x00000000u
#define MB_ICONINFORMATION 0x00000040u
#define MB_ICONWARNING 0x00000030u
#define VK_F8 0x77

extern "C" int _fltused = 0;
extern "C" __declspec(noinline) __attribute__((optnone)) void* memset(void* dst, int value, SIZE_T n) {
    volatile BYTE* d=(volatile BYTE*)dst; for(SIZE_T i=0;i<n;++i)d[i]=(BYTE)value; return dst;
}
extern "C" __declspec(noinline) __attribute__((optnone)) void* memcpy(void* dst,const void* src,SIZE_T n) {
    volatile BYTE* d=(volatile BYTE*)dst; const volatile BYTE* s=(const volatile BYTE*)src;
    for(SIZE_T i=0;i<n;++i)d[i]=s[i]; return dst;
}

static HMODULE g_self=0;
static wchar_t g_ini_path[1024];
static wchar_t g_log_path[1024];
static volatile int g_cached_companion_npc=0;

// Elden Ring 1.17.1 layout used by the current bindings.
static const ULONGLONG WCM_MAIN_PLAYER            = 0x1E508ull;
static const ULONGLONG WCM_SUMMON_BUDDY_MANAGER  = 0x1E538ull;
// Current 1.17.1 WorldChrMan layout: OwnedPtr<CSDebugChrCreator>.
static const ULONGLONG WCM_DEBUG_CHR_CREATOR     = 0x1E648ull;
// Embedded ChrSet<ChrIns> that holds spirit ashes and Torrent in the current 1.17.1 layout.
// Used only as a fallback if SummonBuddyManager::chr_set cannot yield a ChrIns.
static const ULONGLONG WCM_SUMMON_BUDDY_CHR_SET  = 0x10F90ull;

// CSDebugChrCreator layout from current 1.17.1 bindings.
// WorldChrMan::spawn_debug_character only writes these fields then sets spawn=true.
static const ULONGLONG DCC_SPAWN                 = 0x44ull;
static const ULONGLONG DCC_INIT_DATA             = 0xB0ull;
static const ULONGLONG DCC_LAST_CREATED_CHR      = 0x1B0ull;
static const ULONGLONG DCI_SPAWN_POSITION        = 0x00ull;
static const ULONGLONG DCI_NPC_PARAM_ID          = 0x40ull;
static const ULONGLONG DCI_NPC_THINK_PARAM_ID    = 0x44ull;
static const ULONGLONG DCI_EVENT_ENTITY_ID       = 0x48ull;
static const ULONGLONG DCI_TALK_ID               = 0x4Cull;
static const ULONGLONG DCI_NAME                  = 0x50ull; // wchar_t[32]
static const ULONGLONG DCI_CHARA_INIT_PARAM_ID   = 0xCCull;
static const ULONGLONG DCI_SPAWN_ROTATION        = 0x10ull;
static const ULONGLONG DCI_SPAWN_SCALE           = 0x30ull;
static const ULONGLONG DCI_ENEMY_TYPE            = 0xC8ull;

// ChrIns offsets verified against the same 1.17.1 fromsoftware-rs revision used by ER_Summon.
static const ULONGLONG CHR_NPC_PARAM_ID           = 0x60ull;
static const ULONGLONG CHR_TYPE                   = 0x68ull;
static const ULONGLONG CHR_TEAM_TYPE              = 0x6Cull;
static const ULONGLONG CHR_SPECIAL_EFFECT         = 0x178ull;
static const ULONGLONG CHR_LOAD_STATE             = 0x1B4ull;
static const ULONGLONG CHR_FLAGS_1C4              = 0x1C4ull;
static const ULONGLONG CHR_FLAGS_1C5              = 0x1C5ull;
static const ULONGLONG CHR_FLAGS_1C8              = 0x1C8ull;
static const ULONGLONG CHR_ACTIVATION_FLAGS       = 0x1CCull;
static const ULONGLONG CHR_OPACITY_KEY            = 0x238ull;
static const ULONGLONG CHR_OPACITY_KEY_PREV       = 0x23Cull;
static const ULONGLONG CHR_TINT_ALPHA             = 0x240ull;
static const ULONGLONG CHR_TINT_ALPHA_MOD         = 0x244ull;
static const ULONGLONG CHR_CAMOUFLAGE_ALPHA       = 0x248ull;
static const ULONGLONG CHR_BASE_ALPHA             = 0x24Cull;
static const ULONGLONG CHR_BASE_ALPHA_MOD         = 0x250ull;
static const ULONGLONG CHR_DEBUG_FLAGS            = 0x538ull;

static const ULONGLONG MODULE_EVENT               = 0x58ull;
static const ULONGLONG EVENT_REQUEST_ANIM         = 0x18ull;
static const int DEBUG_NPC_ANIMATION_ID            = 20010;
static const int  WHITE_SUMMON_NPC_CHR_TYPE       = 19; // ChrType::WhiteSummonNpc
static const BYTE WHITE_SUMMON_NPC_TEAM_TYPE      = 28; // TeamType::CoopNPC

// SpecialEffect / SpecialEffectEntry offsets.
static const ULONGLONG SPE_HEAD                    = 0x08ull;
static const ULONGLONG SPE_ENTRY_PARAM_DATA        = 0x00ull; // SP_EFFECT_PARAM_ST*
static const ULONGLONG SPE_ENTRY_PARAM_ID          = 0x08ull;
static const ULONGLONG SPE_ENTRY_NEXT              = 0x30ull;
// Confirmed current SpecialEffectEntry layout:
// +0x40 removal_timer = time left until removal
// +0x48 duration      = total duration of the active instance
static const ULONGLONG SPE_ENTRY_REMOVAL_TIMER     = 0x40ull;
static const ULONGLONG SPE_ENTRY_DURATION          = 0x48ull;

// Elden Ring SpEffectParam offsets (Paramdex ER / current generated layout).
// These four *AttackRate fields are post-defense multipliers, so setting only them to 5.0
// gives an unmistakable ~5x damage result without double-multiplying AttackPowerRate.
static const ULONGLONG SEP_EFFECT_ENDURANCE       = 0x08ull; // -1 = permanent
static const ULONGLONG SEP_PHYSICS_ATTACK_RATE     = 0x38ull;
static const ULONGLONG SEP_MAGIC_ATTACK_RATE       = 0x3Cull;
static const ULONGLONG SEP_FIRE_ATTACK_RATE        = 0x40ull;
static const ULONGLONG SEP_THUNDER_ATTACK_RATE     = 0x44ull;
static const ULONGLONG SEP_TARGET_PRIORITY         = 0xF8ull;
static const float RUNTIME_FURY_DAMAGE_RATE         = 5.0f;
static const float RUNTIME_FURY_TARGET_PRIORITY     = 1.0f;

// Reforged 2.3.4.1 exact regulation.bin chain (verified from the user's file):
// Spirit passive wrapper +100 cycles 299020; Fury wrapper +200 cycles 299030.
// 299020 and 299030 differ in exactly one gameplay field: stateInfo 640 vs 641.
// The core row normally lasts only 0.1s. v2.1 does NOT refresh it every 50ms.
// Instead it pins the active entry's timer after one application.
static const int REFORGED_PASSIVE_CORE_SPEFFECT = 299020;
static const int REFORGED_FURY_CORE_SPEFFECT    = 299030;
static const DWORD REFORGED_CORE_MAINTENANCE_MS = 2000;
static const float REFORGED_CORE_PIN_SECONDS     = 3600.0f;
static const DWORD FURY_CACHED_REARM_MS             = 22000;
static const DWORD RESPAWN_MONITOR_MS              = 250;
static const int   RESPAWN_MISSING_VOTES           = 3;
static const ULONGLONG RESPAWN_REQUEST_COOLDOWN_MS = 6000ull;
static const ULONGLONG WARP_RESPAWN_SUPPRESS_MS     = 18000ull;
static const ULONGLONG CHR_TAE_QUEUED_USE_ITEM    = 0x160ull;
static const ULONGLONG CHR_MODULES                = 0x190ull;
static const ULONGLONG MODULE_ACTION_REQUEST      = 0x80ull;

// CSChrActionRequestModule fields.
static const ULONGLONG AR_ACTION_REQUESTS         = 0x10ull;
static const ULONGLONG AR_PREVIOUS_REQUESTS       = 0x18ull;
static const ULONGLONG AR_NEW_PRESSES             = 0x20ull;
static const ULONGLONG AR_QUEUED_INPUTS           = 0x38ull;
static const ULONGLONG AR_POSSIBLE_INPUTS         = 0x98ull;
static const ULONGLONG ACTION_USE_ITEM_BIT        = (1ull << 7);

// SummonBuddyManager fields. v0.6 reads request/alive state and only forces the
// activation/warning availability flags; it does NOT forge the summon request or alive flag.
static const ULONGLONG SBM_REQUEST_SPEFFECT       = 0x20ull;
static const ULONGLONG SBM_ACTIVE_SPEFFECT        = 0x24ull;
static const ULONGLONG SBM_DISAPPEAR_REQUESTED    = 0x28ull;
static const ULONGLONG SBM_PLAYER_HAS_ALIVE       = 0xB4ull;
static const ULONGLONG SBM_ACTIVATION             = 0xB5ull;
static const ULONGLONG SBM_PREV_ACTIVATION        = 0xB6ull;
static const ULONGLONG SBM_WARN                   = 0xB7ull;
