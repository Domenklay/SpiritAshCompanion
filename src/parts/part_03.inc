    if(g_cfg.preset==2)return 100000010;  // Mimic Tear
    return 100000020;                     // Nepheli Loux Puppet / Tarnished preset
}

static void log_preset(){
    if(g_cfg.preset==0)log_line("[PRESET] 0 = Cleanrot Knight Finlay +10.");
    else if(g_cfg.preset==1)log_line("[PRESET] 1 = Jarwight Puppet +10 (pot thrower).");
    else if(g_cfg.preset==2)log_line("[PRESET] 2 = Mimic Tear +10 (player copy).");
    else log_line("[PRESET] 3 = Tarnished +10 (Nepheli Loux Puppet, melee human companion).");
}

// ChrSet<ChrIns> layout from current Elden Ring bindings.
// +0x10 u32 capacity, +0x18 ChrSetEntry* entries; each ChrSetEntry is 0x10 bytes, ChrIns* at +0x0 and load status at +0x8.
// IMPORTANT: the upstream ChrSet::characters() iterator does NOT filter on load status; it only requires a non-null ChrIns.
// v1.1 incorrectly accepted only statuses 1/2/4. v1.2 also had a typo in the WorldChrMan fallback offset (0x10EF90 instead of 0x10F90).
// v1.4 resolves characters through the native ChrSet vtable first, matching the game API; raw entries are fallback only.
static void log_chr_status(BYTE status,const char* source){
    if(source && source[0]=='m'){
        if(status==0)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=0 (Unloaded), status filter intentionally ignored.");
        else if(status==1)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=1 (Initializing).");
        else if(status==2)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=2 (Active).");
        else if(status==3)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=3 (NetworkInitializing), accepted by v1.2.");
        else if(status==4)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=4 (ReadyForActivation).");
        else if(status==5)log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; load status=5 (Unloading), status filter intentionally ignored.");
        else log_line("[FURY] Buddy ChrIns resolved from manager ChrSet; unknown load status, accepted by v1.2.");
    }else{
        if(status==3)log_line("[FURY] Buddy ChrIns resolved from WorldChrMan summon_buddy_chr_set fallback; status=3.");
        else log_line("[FURY] Buddy ChrIns resolved from WorldChrMan summon_buddy_chr_set fallback.");
    }
}

typedef unsigned int (__fastcall *ChrSetGetCapacityFn)(void*);
typedef void* (__fastcall *ChrSetSafeGetChrFn)(void*,unsigned int);

static ULONGLONG get_chr_from_set_vtable(ULONGLONG set,const char* source){
    if(!set||!readable_range((void*)set,8))return 0;
    ULONGLONG vt=*(volatile ULONGLONG*)set;
    if(!vt||!readable_range((void*)vt,16))return 0;
    ULONGLONG fn_cap=*(volatile ULONGLONG*)(vt+0x0);
    ULONGLONG fn_get=*(volatile ULONGLONG*)(vt+0x8);
    if(!fn_cap||!fn_get||!readable_range((void*)fn_cap,1)||!readable_range((void*)fn_get,1))return 0;
    ChrSetGetCapacityFn get_cap=(ChrSetGetCapacityFn)fn_cap;
    ChrSetSafeGetChrFn safe_get=(ChrSetSafeGetChrFn)fn_get;
    unsigned int cap=get_cap((void*)set);
    if(cap==0||cap>4096)return 0;
    for(unsigned int i=0;i<cap;++i){
        void* chr=safe_get((void*)set,i);
        if(chr&&readable_range(chr,0x40)){
            // Quiet hot path: v2.0 refreshes the real Fury core every 50 ms.
            // Repeated resolution logging here would create a huge log file.
            (void)source;
            return (ULONGLONG)chr;
        }
    }
    return 0;
}

static ULONGLONG get_chr_from_set_raw(ULONGLONG set,const char* source){
    if(!set||!readable_range((void*)set,0x20))return 0;
    unsigned int cap=*(volatile unsigned int*)(set+0x10);
    ULONGLONG entries=*(volatile ULONGLONG*)(set+0x18);
    if(!entries||cap==0||cap>4096)return 0;
    // Do not require the entire capacity allocation to be readable: validate each entry separately.
    for(unsigned int i=0;i<cap;++i){
        BYTE* e=(BYTE*)(entries+(ULONGLONG)i*0x10ull);
        if(!readable_range(e,0x10))continue;
        ULONGLONG chr=*(volatile ULONGLONG*)(e+0x0);
        BYTE status=*(volatile BYTE*)(e+0x8);
        if(chr&&readable_range((void*)chr,0x40)){
            (void)status; (void)source;
            return chr;
        }
    }
    return 0;
}

static ULONGLONG get_chr_from_set(ULONGLONG set,const char* source){
    ULONGLONG chr=get_chr_from_set_vtable(set,source);
    if(chr)return chr;
    return get_chr_from_set_raw(set,source);
}

static ULONGLONG get_buddy_chr(ULONGLONG world,ULONGLONG mgr){
    if(mgr&&readable_range((void*)(mgr+SBM_CHR_SET),8)){
        ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);
        ULONGLONG chr=get_chr_from_set(set,"manager");
        if(chr)return chr;
    }
    // Fallback: WorldChrMan embeds the same summon-buddy ChrSet directly.
    if(world&&readable_range((void*)(world+WCM_SUMMON_BUDDY_CHR_SET),0x20)){
        ULONGLONG chr=get_chr_from_set(world+WCM_SUMMON_BUDDY_CHR_SET,"world");
        if(chr)return chr;
    }
    return 0;
}

// v2.4 critical fix: summon_buddy_chr_set contains Spirit Ashes AS WELL AS Torrent.
// Never treat the first non-null entry as Dolores. Select by the actual ChrIns::npc_param_id at +0x60.
static ULONGLONG get_chr_from_set_vtable_by_npc(ULONGLONG set,int expected_npc){
    if(!set||!readable_range((void*)set,8))return 0;
    ULONGLONG vt=*(volatile ULONGLONG*)set;if(!vt||!readable_range((void*)vt,16))return 0;
    ULONGLONG fn_cap=*(volatile ULONGLONG*)(vt+0x0),fn_get=*(volatile ULONGLONG*)(vt+0x8);
    if(!fn_cap||!fn_get||!readable_range((void*)fn_cap,1)||!readable_range((void*)fn_get,1))return 0;
    ChrSetGetCapacityFn get_cap=(ChrSetGetCapacityFn)fn_cap;ChrSetSafeGetChrFn safe_get=(ChrSetSafeGetChrFn)fn_get;
    unsigned int cap=get_cap((void*)set);if(!cap||cap>4096)return 0;
    for(unsigned int i=0;i<cap;++i){
        void* chr=safe_get((void*)set,i);if(!chr||!readable_range(chr,0x70))continue;
        int npc=*(volatile int*)((BYTE*)chr+CHR_NPC_PARAM_ID);if(npc==expected_npc)return (ULONGLONG)chr;
    }
    return 0;
}
static ULONGLONG get_chr_from_set_raw_by_npc(ULONGLONG set,int expected_npc){
    // v3.7.1 FINLAY NO-HITCH: NEVER VirtualQuery every ChrSet entry.
    // A large/partially populated SummonBuddy ChrSet made the old "bounded" lookup stall the game
    // for seconds while the selected companion was still entering the set.  SEH-protected scalar reads are cheap and
    // safely turn stale entries into misses during transitions.
    if(!set)return 0;
    unsigned int cap=0;ULONGLONG entries=0;
    __try{cap=*(volatile unsigned int*)(set+0x10);entries=*(volatile ULONGLONG*)(set+0x18);}__except(1){return 0;}
    if(!entries||!cap||cap>4096)return 0;
    for(unsigned int i=0;i<cap;++i){
        ULONGLONG chr=0;int npc=0;
        if(!seh_read_u64((volatile ULONGLONG*)(entries+(ULONGLONG)i*0x10ull),&chr)||!chr)continue;
        if(!seh_read_i32((volatile int*)(chr+CHR_NPC_PARAM_ID),&npc))continue;
        if(npc==expected_npc)return chr;
    }
    return 0;
}
// v3.8.0: one-shot identity capture for the selected companion. This deliberately uses ChrSet::safe_get only
// during summon/transition acquisition. Unlike the old hitching path it performs NO VirtualQuery per
// ChrIns entry and it is never used by the healthy-state monitor or the 22s Fury rearm.
static ULONGLONG get_companion_vtable_once_no_vq(ULONGLONG world,ULONGLONG mgr,int expected_npc){
    ULONGLONG sets[2]={0,0};int count=0;
    if(mgr&&readable_range((void*)(mgr+SBM_CHR_SET),8)){
        ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);if(set)sets[count++]=set;
    }
    if(world&&readable_range((void*)(world+WCM_SUMMON_BUDDY_CHR_SET),0x20))sets[count++]=world+WCM_SUMMON_BUDDY_CHR_SET;
    for(int si=0;si<count;++si){
        ULONGLONG set=sets[si];if(!set||!readable_range((void*)set,8))continue;
        ULONGLONG vt=0; if(!seh_read_u64((volatile ULONGLONG*)set,&vt)||!vt||!readable_range((void*)vt,16))continue;
        ULONGLONG fn_cap=0,fn_get=0;
        if(!seh_read_u64((volatile ULONGLONG*)(vt+0x0),&fn_cap)||!seh_read_u64((volatile ULONGLONG*)(vt+0x8),&fn_get))continue;
        if(!fn_cap||!fn_get||!readable_range((void*)fn_cap,1)||!readable_range((void*)fn_get,1))continue;
        ChrSetGetCapacityFn get_cap=(ChrSetGetCapacityFn)fn_cap;ChrSetSafeGetChrFn safe_get=(ChrSetSafeGetChrFn)fn_get;
        unsigned int cap=get_cap((void*)set);
        if(!cap||cap>4096)continue;
        unsigned int lim=cap>512?512:cap;
        ULONGLONG fallback=0;int fallback_npc=0;unsigned int non_torrent=0;
        for(unsigned int i=0;i<lim;++i){
            void* p=safe_get((void*)set,i);
            ULONGLONG chr=(ULONGLONG)p;if(!chr)continue;
            int npc=0;if(!seh_read_i32((volatile int*)(chr+CHR_NPC_PARAM_ID),&npc)||npc<=0)continue;
            if(npc==expected_npc){g_cached_companion_npc=npc;return chr;}
            if(npc==80020000)continue; // Torrent
            ++non_torrent;if(!fallback){fallback=chr;fallback_npc=npc;}
        }
        if(fallback&&non_torrent==1){
            g_cached_companion_npc=fallback_npc;
            log_i32_line("[IDENTITY] FALLBACK: selected the only non-Torrent SummonBuddy candidate NpcParam=",fallback_npc);
            return fallback;
        }
    }
    return 0;
}

static ULONGLONG get_buddy_chr_exact(ULONGLONG world,ULONGLONG mgr,int expected_npc){
    if(mgr&&readable_range((void*)(mgr+SBM_CHR_SET),8)){
        ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);
        ULONGLONG chr=get_chr_from_set_vtable_by_npc(set,expected_npc);if(!chr)chr=get_chr_from_set_raw_by_npc(set,expected_npc);if(chr)return chr;
    }
    if(world&&readable_range((void*)(world+WCM_SUMMON_BUDDY_CHR_SET),0x20)){
        ULONGLONG set=world+WCM_SUMMON_BUDDY_CHR_SET;ULONGLONG chr=get_chr_from_set_vtable_by_npc(set,expected_npc);if(!chr)chr=get_chr_from_set_raw_by_npc(set,expected_npc);if(chr)return chr;
    }
    return 0;
}
static void log_buddy_chrset_snapshot(ULONGLONG mgr){
    if(!mgr||!readable_range((void*)(mgr+SBM_CHR_SET),8))return;ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);if(!set||!readable_range((void*)set,8))return;
    ULONGLONG vt=*(volatile ULONGLONG*)set;if(!vt||!readable_range((void*)vt,16))return;ChrSetGetCapacityFn get_cap=(ChrSetGetCapacityFn)*(volatile ULONGLONG*)(vt+0);ChrSetSafeGetChrFn safe_get=(ChrSetSafeGetChrFn)*(volatile ULONGLONG*)(vt+8);if(!get_cap||!safe_get)return;
    unsigned int cap=get_cap((void*)set);if(cap>4096)cap=4096;unsigned int shown=0;
    log_line("[IDENTITY] SummonBuddy ChrSet snapshot follows (this set can also contain Torrent).");
    for(unsigned int i=0;i<cap&&shown<16;++i){void* chr=safe_get((void*)set,i);if(!chr||!readable_range(chr,0x70))continue;++shown;int npc=*(volatile int*)((BYTE*)chr+CHR_NPC_PARAM_ID);int ctype=*(volatile int*)((BYTE*)chr+CHR_TYPE);int team=(int)*(volatile BYTE*)((BYTE*)chr+CHR_TEAM_TYPE);log_i32_line("[IDENTITY] candidate NpcParam=",npc);log_i32_line("[IDENTITY] candidate ChrType=",ctype);log_i32_line("[IDENTITY] candidate TeamType=",team);}
    if(!shown)log_line("[IDENTITY] ChrSet currently has no readable ChrIns entries.");
}

typedef ULONGLONG (__fastcall *ApplySpEffectFn)(void*,int,BYTE);
typedef ULONGLONG (__fastcall *RemoveSpEffectFn)(void*,int);
static ApplySpEffectFn g_apply_speffect=0;
static RemoveSpEffectFn g_remove_speffect=0;

static ApplySpEffectFn scan_apply_speffect(){
    // Mapper pattern used by current fromsoftware-rs: caller loads SpEffect 0x147F then calls ChrIns::apply_speffect.
    static const BYTE pat[]={0x48,0x8B,0xC8,0x45,0x33,0xC0,0xBA,0x7F,0x14,0x00,0x00,0xE8};
    static const char mask[]="xxxxxxxxxxxx";
    BYTE* p=scan_text(pat,mask,sizeof(pat));
    if(!p)return 0;
    BYTE* target=p+16+(LONGLONG)read_i32(p+12);
    ModuleInfo mi;SectionView text;if(!get_main_module_info(&mi)||!get_section(&mi,".text",&text))return 0;
    if(!ptr_in_section(target,&text))return 0;
    return (ApplySpEffectFn)target;
}
