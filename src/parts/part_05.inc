        *(volatile DWORD*)(player+CHR_TAE_QUEUED_USE_ITEM)=st->queued_raw;
        *(volatile ULONGLONG*)(ar+AR_ACTION_REQUESTS)|=ACTION_USE_ITEM_BIT;
        *(volatile ULONGLONG*)(ar+AR_PREVIOUS_REQUESTS)&=~ACTION_USE_ITEM_BIT;
        *(volatile ULONGLONG*)(ar+AR_NEW_PRESSES)|=ACTION_USE_ITEM_BIT;
        *(volatile ULONGLONG*)(ar+AR_QUEUED_INPUTS)|=ACTION_USE_ITEM_BIT;
        *(volatile ULONGLONG*)(ar+AR_POSSIBLE_INPUTS)|=ACTION_USE_ITEM_BIT;
        return;
    }
    if(*(volatile DWORD*)(player+CHR_TAE_QUEUED_USE_ITEM)==st->queued_raw)
        *(volatile DWORD*)(player+CHR_TAE_QUEUED_USE_ITEM)=st->saved_queued;
    ULONGLONG* fields[5]={
        (ULONGLONG*)(ar+AR_ACTION_REQUESTS),(ULONGLONG*)(ar+AR_PREVIOUS_REQUESTS),
        (ULONGLONG*)(ar+AR_NEW_PRESSES),(ULONGLONG*)(ar+AR_QUEUED_INPUTS),
        (ULONGLONG*)(ar+AR_POSSIBLE_INPUTS)
    };
    ULONGLONG saved[5]={st->saved_action,st->saved_prev,st->saved_new,st->saved_queued_inputs,st->saved_possible};
    for(int i=0;i<5;++i){
        ULONGLONG cur=*(volatile ULONGLONG*)fields[i];
        cur=(cur&~ACTION_USE_ITEM_BIT)|(saved[i]&ACTION_USE_ITEM_BIT);
        *(volatile ULONGLONG*)fields[i]=cur;
    }
    st->active=FALSE;
    log_line("[FURY-ITEM] USE_ITEM pulse finished and the player's previous input/item state was restored.");
}

static void prepare_debug_npc_visible(ULONGLONG chr,BOOL request_anim){
    if(!chr||!readable_range((void*)chr,0x540))return;
    // v2.0 deliberately does NOT rewrite ChrType or TeamType here.
    // v1.7 proved role rewriting can make a valid Spirit entity disappear.  This probe only
    // asks whether CSDebugChrCreator can render a c0000 human after the same human resources
    // used by Dolores are loaded.
    *(volatile DWORD*)(chr+CHR_LOAD_STATE)&=~(1u<<7);              // extinction_death = false
    *(volatile BYTE*)(chr+CHR_FLAGS_1C4)|=(BYTE)((1u<<1)|(1u<<3));
    BYTE f1c5=*(volatile BYTE*)(chr+CHR_FLAGS_1C5);
    f1c5=(BYTE)((f1c5&~(1u<<7))|(1u<<3));
    *(volatile BYTE*)(chr+CHR_FLAGS_1C5)=f1c5;
    *(volatile BYTE*)(chr+CHR_FLAGS_1C8)|=(BYTE)(1u<<4);
    *(volatile DWORD*)(chr+CHR_ACTIVATION_FLAGS)|=(1u<<3);
    DWORD df=*(volatile DWORD*)(chr+CHR_DEBUG_FLAGS);
    df&=~((1u<<3)|(1u<<4)|(1u<<5)|(1u<<8)|(1u<<10)|(1u<<11)|(1u<<12));
    *(volatile DWORD*)(chr+CHR_DEBUG_FLAGS)=df;
    *(volatile float*)(chr+CHR_OPACITY_KEY)=1.0f;
    *(volatile float*)(chr+CHR_OPACITY_KEY_PREV)=1.0f;
    *(volatile float*)(chr+CHR_TINT_ALPHA)=1.0f;
    *(volatile float*)(chr+CHR_TINT_ALPHA_MOD)=1.0f;
    *(volatile float*)(chr+CHR_CAMOUFLAGE_ALPHA)=1.0f;
    *(volatile float*)(chr+CHR_BASE_ALPHA)=1.0f;
    *(volatile float*)(chr+CHR_BASE_ALPHA_MOD)=1.0f;
    if(request_anim&&readable_range((void*)(chr+CHR_MODULES),8)){
        ULONGLONG modules=*(volatile ULONGLONG*)(chr+CHR_MODULES);
        if(modules&&readable_range((void*)(modules+MODULE_EVENT),8)){
            ULONGLONG ev=*(volatile ULONGLONG*)(modules+MODULE_EVENT);
            if(ev&&readable_range((void*)(ev+EVENT_REQUEST_ANIM),4))
                *(volatile int*)(ev+EVENT_REQUEST_ANIM)=DEBUG_NPC_ANIMATION_ID;
        }
    }
}

static BOOL convert_buddy_to_white_summon_npc(ULONGLONG world,ULONGLONG mgr){
    ULONGLONG chr=get_buddy_chr(world,mgr);
    if(!chr||!readable_range((void*)chr,0x70))return FALSE;
    int old_type=*(volatile int*)(chr+CHR_TYPE);
    BYTE old_team=*(volatile BYTE*)(chr+CHR_TEAM_TYPE);
    *(volatile int*)(chr+CHR_TYPE)=WHITE_SUMMON_NPC_CHR_TYPE;
    *(volatile BYTE*)(chr+CHR_TEAM_TYPE)=WHITE_SUMMON_NPC_TEAM_TYPE;
    // Keep the already-loaded Spirit Ash entity, but ask the engine to treat it as a white NPC summon.
    *(volatile BYTE*)(chr+CHR_FLAGS_1C4)|=(BYTE)(1u<<1);
    log_line("[WHITE-NPC] Existing Spirit Ash ChrIns reclassified as ChrType=19 WhiteSummonNpc, TeamType=28 CoopNPC.");
    if(old_type==WHITE_SUMMON_NPC_CHR_TYPE && old_team==WHITE_SUMMON_NPC_TEAM_TYPE)
        log_line("[WHITE-NPC] Role was already WhiteSummonNpc/CoopNPC.");
    return TRUE;
}

static BOOL apply_real_spirit_fury_to_chr(ULONGLONG chr){
    if(!g_apply_speffect||!g_remove_speffect||!chr||!readable_range((void*)chr,0x70))return FALSE;
    int goods=0,trigger=0;get_companion_ids(&goods,&trigger);
    g_remove_speffect((void*)chr,REFORGED_PASSIVE_CORE_SPEFFECT);
    g_remove_speffect((void*)chr,trigger+100);
    if(!chr_has_speffect_raw(chr,REFORGED_FURY_CORE_SPEFFECT))g_apply_speffect((void*)chr,REFORGED_FURY_CORE_SPEFFECT,1);
    BOOL fury_core=chr_has_speffect_raw(chr,REFORGED_FURY_CORE_SPEFFECT);
    if(fury_core)fury_core=pin_speffect_entry_lifetime(chr,REFORGED_FURY_CORE_SPEFFECT,REFORGED_CORE_PIN_SECONDS);
    BOOL passive_core=chr_has_speffect_raw(chr,REFORGED_PASSIVE_CORE_SPEFFECT);(void)goods;
    return fury_core&&!passive_core;
}
static BOOL apply_real_spirit_fury(ULONGLONG world,ULONGLONG mgr){
    ULONGLONG chr=get_buddy_chr_exact(world,mgr,get_expected_companion_npc_param());
    if(!chr)return FALSE;
    return apply_real_spirit_fury_to_chr(chr);
}


// Exact 0xA0-byte Dolores BuddyParam row (26200000) from the user's Reforged 2.3.4.1 regulation.bin.
// We scan only committed writable process memory and patch matching loaded copies in RAM.
// The file on disk is never opened or modified.
static const BYTE DOLORES_BUDDY_ROW_ORIGINAL[160]={
0x00,0x00,0x00,0x00,0x70,0xFF,0x03,0x00,0x32,0xE1,0xF5,0x05,0x32,0xE1,0xF5,0x05,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x66,0x66,0xA6,0x3F,0x66,0x66,0xA6,0xBF,
0x00,0x00,0xA0,0x41,0x00,0x00,0x00,0x00,0x90,0x93,0x04,0x00,0x91,0x93,0x04,0x00,
0x92,0x93,0x04,0x00,0x93,0x93,0x04,0x00,0x94,0x93,0x04,0x00,0x95,0x93,0x04,0x00,
0x96,0x93,0x04,0x00,0x97,0x93,0x04,0x00,0x98,0x93,0x04,0x00,0x99,0x93,0x04,0x00,
0x9A,0x93,0x04,0x00,0x58,0x5C,0x00,0x00,0x55,0xEC,0x00,0x00,0x96,0x05,0x00,0x00,
0xA0,0x05,0x00,0x00,0xBE,0x4B,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0x8C,0x05,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

static BOOL bytes_equal_160(const BYTE* p,const BYTE* sig){
    // Fast reject on the distinctive trigger/NPC prefix, then exact compare.
    if(*(const volatile DWORD*)(p+4)!=262000u)return FALSE;
    if(*(const volatile int*)(p+8)!=100000050)return FALSE;
    if(*(const volatile int*)(p+12)!=100000050)return FALSE;
    for(int i=0;i<160;++i)if(p[i]!=sig[i])return FALSE;
    return TRUE;
}

static int patch_loaded_dolores_buddy_to_melina(){
    // Protection constants from WinNT.h; avoiding another SDK dependency keeps this DLL self-contained.
    const DWORD PAGE_READWRITE_X=0x04u, PAGE_WRITECOPY_X=0x08u, PAGE_EXECUTE_WRITECOPY_X=0x80u;
    int patched=0;
    BYTE* addr=(BYTE*)0x10000ull;
    for(;;){
        MEMORY_BASIC_INFORMATION_X64 mbi;
        SIZE_T got=VirtualQuery(addr,&mbi,sizeof(mbi));
        if(!got)break;
        BYTE* base=(BYTE*)mbi.BaseAddress;
        SIZE_T size=mbi.RegionSize;
        BYTE* next=base+size;
        if(next<=addr)break;

        DWORD p=mbi.Protect & 0xFFu;
        BOOL writable=(p==PAGE_READWRITE_X||p==PAGE_WRITECOPY_X||p==PAGE_EXECUTE_READWRITE||p==PAGE_EXECUTE_WRITECOPY_X);
        if(mbi.State==MEM_COMMIT && writable && !(mbi.Protect&PAGE_GUARD) && !(mbi.Protect&PAGE_NOACCESS) && size>=160){
            // PARAM rows are naturally 4-byte aligned. Scan in chunks but never cross the region boundary.
            BYTE* end=base+size-160;
            for(BYTE* q=base;q<=end;q+=4){
                if(*(volatile DWORD*)(q+4)!=262000u)continue;
                if(!bytes_equal_160(q,DOLORES_BUDDY_ROW_ORIGINAL))continue;
                // BUDDY_PARAM_ST: +0x8 npcParamId, +0xC npcThinkParamId,
                // +0x24 appearOnAroundSekihi, +0x25 disablePCTargetShare, +0x26 pcFollowType,
                // +0x54 npcPlayerInitParamId, +0x58 generateAnimId.
                *(volatile int*)(q+0x08)=21800000; // Melina NpcParam in this exact regulation.bin.
                *(volatile int*)(q+0x0C)=21800000; // Melina's summon-capable NpcThinkParam.
                *(volatile int*)(q+0x10)=-1;
                *(volatile int*)(q+0x14)=-1;
                *(volatile BYTE*)(q+0x24)=0;
                *(volatile BYTE*)(q+0x25)=0;       // keep PC target sharing enabled.
                *(volatile BYTE*)(q+0x26)=0;       // normal PC follow/warp mode.
                *(volatile int*)(q+0x54)=-1;       // non-c0000 NPC; do not use Dolores CharaInit.
                *(volatile int*)(q+0x58)=-1;       // use NPC's own normal generation animation path.
                ++patched;
            }
        }
        addr=next;
    }
    return patched;
}


// v2.3 exact runtime signatures derived from the user's Reforged 2.3.4.1 regulation.bin.
// The scanner is deliberately incremental: at most 2 MiB of committed writable memory is inspected
// per game-loop pass, after the known-working Dolores has already spawned. This prevents the v2.2
// startup stall caused by synchronously walking the complete address space.
static const BYTE MELINA_NPC_PREFIX[64]={
0x00,0x00,0x00,0x00,0x28,0x55,0x00,0x00,0x0B,0x00,0x00,0x00,0xB0,0xAD,0x01,0x00,
0x00,0x00,0x34,0x43,0x66,0x66,0x26,0x40,0x00,0x00,0x00,0x3F,0x64,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0xF8,0x0D,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x20,0x41,0x00,0x00,0xFF,0xFF
};
static const BYTE MELINA_NPC_MARKER_140[32]={
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,0x01,0x34,0x01,0xB4,0x01,0x00,
0x00,0x00,0x00,0x00,0x66,0x66,0x26,0x40,0x00,0x00,0x00,0x3F,0x00,0x00,0x05,0x00
};
static const BYTE MELINA_THINK_PREFIX[64]={
0x00,0x00,0x00,0x00,0x30,0x75,0x00,0x00,0x90,0x53,0x03,0x00,0x00,0x00,0x00,0x04,
0xFF,0xFF,0xFF,0xFF,0x00,0x00,0xA0,0x40,0x00,0x00,0x00,0x41,0x00,0x00,0x00,0x00,
0x00,0x3C,0x1C,0x46,0xBB,0x0B,0x00,0x00,0x00,0x00,0x80,0x3F,0xFF,0xFF,0xFF,0xFF,
0xFF,0xFF,0xFF,0xFF,0x64,0x00,0x00,0x00,0x00,0x00,0x0A,0x00,0x0F,0x27,0x0F,0x27
};

static BOOL bytes_equal_n(const BYTE* a,const BYTE* b,SIZE_T n){
    if(!a||!b)return FALSE;
    for(SIZE_T i=0;i<n;++i)if(a[i]!=b[i])return FALSE;
    return TRUE;
}

struct DeferredParamScan {
    ULONGLONG cursor;
    ULONGLONG region_base;
    SIZE_T region_size;
    SIZE_T region_offset;
    ULONGLONG bytes_scanned;
    ULONGLONG buddy_row;
    ULONGLONG melina_npc_row;
    ULONGLONG melina_think_row;
    BOOL active;
    BOOL finished;
    BOOL patched;
    BOOL progress_512_logged;
};
