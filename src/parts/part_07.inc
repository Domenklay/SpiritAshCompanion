    if(*(volatile BYTE*)(creator+DCC_SPAWN)){log_line("[NPC] Debug creator already has a pending spawn request; not overwriting it.");return FALSE;}
    Vec4 p;if(!get_player_position(player,&p)){log_line("[NPC] Could not read player position for debug NPC probe.");return FALSE;}
    BYTE* init=(BYTE*)(creator+DCC_INIT_DATA);
    if(!readable_range(init,0x100)){log_line("[NPC] Debug creator init_data is unreadable.");return FALSE;}
    volatile float* pos=(volatile float*)(init+DCI_SPAWN_POSITION);
    pos[0]=p.x+((float)g_cfg.debug_offset_x_cm/100.0f);pos[1]=p.y;pos[2]=p.z;pos[3]=0.0f;
    volatile float* rot=(volatile float*)(init+DCI_SPAWN_ROTATION);
    rot[0]=0.0f;rot[1]=0.0f;rot[2]=0.0f;rot[3]=0.0f;
    volatile float* scale=(volatile float*)(init+DCI_SPAWN_SCALE);
    scale[0]=1.0f;scale[1]=1.0f;scale[2]=1.0f;scale[3]=1.0f;
    *(volatile BYTE*)(init+DCI_ENEMY_TYPE)=0; // same setting as ER_Summon's validated non-player c4205.
    *(volatile int*)(init+DCI_NPC_PARAM_ID)=g_cfg.debug_npc_param_id;
    *(volatile int*)(init+DCI_NPC_THINK_PARAM_ID)=g_cfg.debug_npc_think_param_id;
    *(volatile int*)(init+DCI_EVENT_ENTITY_ID)=g_cfg.debug_event_entity_id;
    *(volatile int*)(init+DCI_TALK_ID)=g_cfg.debug_talk_id;
    wchar_t name[32];build_debug_chr_name(name,g_cfg.debug_chr_id);
    volatile wchar_t* dst=(volatile wchar_t*)(init+DCI_NAME);for(int i=0;i<32;++i)dst[i]=name[i];
    *(volatile int*)(init+DCI_CHARA_INIT_PARAM_ID)=g_cfg.debug_chara_init_param_id;
    *(volatile ULONGLONG*)(creator+DCC_LAST_CREATED_CHR)=0;
    *(volatile BYTE*)(creator+DCC_SPAWN)=1;
    log_line("[NPC] c0000 CSDebugChrCreator request submitted using Dolores-derived params. Waiting for last_created_chr.");
    return TRUE;
}

static ULONGLONG poll_debug_npc_created(ULONGLONG world){
    ULONGLONG creator=get_debug_chr_creator(world);if(!creator)return 0;
    ULONGLONG chr=*(volatile ULONGLONG*)(creator+DCC_LAST_CREATED_CHR);
    if(chr&&readable_range((void*)chr,0x80))return chr;
    return 0;
}

// v2.9: raw-only exact companion lookup. Never calls ChrSet vtable/native methods.
static ULONGLONG get_exact_companion_raw_only(ULONGLONG world,ULONGLONG mgr,int expected_npc){
    if(mgr&&readable_range((void*)(mgr+SBM_CHR_SET),8)){
        ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);
        ULONGLONG chr=get_chr_from_set_raw_by_npc(set,expected_npc);
        if(chr)return chr;
    }
    // Fallback is also a raw-memory read; no engine function is invoked.
    if(world&&readable_range((void*)(world+WCM_SUMMON_BUDDY_CHR_SET),0x20)){
        ULONGLONG chr=get_chr_from_set_raw_by_npc(world+WCM_SUMMON_BUDDY_CHR_SET,expected_npc);
        if(chr)return chr;
    }
    return 0;
}

// Monitor-only raw lookup optimized for very low overhead: validate the contiguous entry array once,
// then touch only non-null ChrIns pointers. If the array cannot be validated as one readable range, return 0;
// the bounded summon cycle still uses the proven v2.9 per-entry verifier.
static ULONGLONG get_exact_companion_raw_monitor(ULONGLONG world,ULONGLONG mgr,int expected_npc){
    ULONGLONG sets[2]={0,0};
    int count=0;
    if(mgr&&readable_range((void*)(mgr+SBM_CHR_SET),8)){
        ULONGLONG set=*(volatile ULONGLONG*)(mgr+SBM_CHR_SET);if(set)sets[count++]=set;
    }
    if(world&&readable_range((void*)(world+WCM_SUMMON_BUDDY_CHR_SET),0x20))sets[count++]=world+WCM_SUMMON_BUDDY_CHR_SET;
    for(int si=0;si<count;++si){
        ULONGLONG set=sets[si];
        if(!set||!readable_range((void*)set,0x20))continue;
        unsigned int cap=*(volatile unsigned int*)(set+0x10);
        ULONGLONG entries=*(volatile ULONGLONG*)(set+0x18);
        if(!entries||!cap||cap>4096)continue;
        SIZE_T bytes=(SIZE_T)cap*0x10ull;
        if(!readable_range((void*)entries,bytes))continue;
        for(unsigned int i=0;i<cap;++i){
            ULONGLONG chr=*(volatile ULONGLONG*)(entries+(ULONGLONG)i*0x10ull);
            if(!chr||!readable_range((void*)(chr+CHR_NPC_PARAM_ID),4))continue;
            if(*(volatile int*)(chr+CHR_NPC_PARAM_ID)==expected_npc)return chr;
        }
    }
    return 0;
}

// One bounded Fury transition. Prefer Reforged's companion-specific +200 wrapper,
// then fall back to the verified shared Fury core 299030 if the wrapper has not produced it.
static BOOL apply_safe_fury_once(ULONGLONG chr){
    if(!g_apply_speffect||!g_remove_speffect||!chr||!readable_range((void*)chr,0x200))return FALSE;
    int goods=0,trigger=0;get_companion_ids(&goods,&trigger);(void)goods;
    int passive_wrapper=trigger+100;
    int fury_wrapper=trigger+200;

    log_i32_line("[FURY] Removing passive wrapper SpEffect=",passive_wrapper);
    g_remove_speffect((void*)chr,passive_wrapper);
    g_remove_speffect((void*)chr,REFORGED_PASSIVE_CORE_SPEFFECT);

    log_i32_line("[FURY] Applying companion Fury wrapper SpEffect=",fury_wrapper);
    g_apply_speffect((void*)chr,fury_wrapper,1);
    Sleep(300);

    BOOL wrapper_present=chr_has_speffect_raw(chr,fury_wrapper);
    if(!wrapper_present){
        log_line("[FURY] FAILED: companion Fury wrapper was not found after the one-shot application.");
        return FALSE;
    }
    log_line("[FURY] Selected companion Fury wrapper is present.");
    BOOL permanent=make_fury_wrapper_permanent(chr,fury_wrapper);
    log_line(permanent?"[FURY] SUCCESS: wrapper SpEffectParam.effectEndurance patched to -1 (permanent).":"[FURY] WARNING: wrapper endurance Param patch could not be confirmed.");

    BOOL core_present=chr_has_speffect_raw(chr,REFORGED_FURY_CORE_SPEFFECT);
    if(core_present)log_line("[FURY] Reforged Fury core 299030 appeared through wrapper.");
    else log_line("[FURY] Core 299030 was not visible at this instant; persistent wrapper remains authoritative.");

    BOOL passive_core=chr_has_speffect_raw(chr,REFORGED_PASSIVE_CORE_SPEFFECT);
    if(passive_core)log_line("[FURY] WARNING: passive core 299020 is still present.");
    return permanent&&!passive_core;
}

// v3.5: refresh Reforged's internal ~30s Fury state without any search or SpEffect traversal.
// The exact ChrIns was already found after summon. We only verify its cached NpcParam with one
// SEH-guarded direct read, then restart the companion-specific +200 wrapper in place.
static BOOL rearm_cached_fury(ULONGLONG chr,int expected_npc){
    if(!chr||!g_apply_speffect||!g_remove_speffect)return FALSE;
    int npc=0;
    int cached_expected=g_cached_companion_npc?g_cached_companion_npc:expected_npc;
    if(!seh_read_i32((volatile int*)(chr+CHR_NPC_PARAM_ID),&npc)||npc!=cached_expected)return FALSE;
    int goods=0,trigger=0;get_companion_ids(&goods,&trigger);(void)goods;
    int fury_wrapper=trigger+200;
    g_remove_speffect((void*)chr,fury_wrapper);
    g_apply_speffect((void*)chr,fury_wrapper,1);
    return TRUE;
}

// v3.0: run the already-proven v2.8 handshake as one bounded transaction.
// No permanent manager writes occur here; after the transaction it returns to the low-frequency monitor.
static ULONGLONG run_safe_summon_cycle(BYTE* world_global,int expected_npc,const char* reason){
    if(reason)log_line(reason);
    ULONGLONG world=0,mgr=0,player=0;
    for(int wait=0;wait<20;++wait){
        if(readable_range(world_global,8)){
            world=*(volatile ULONGLONG*)world_global;
            if(world&&readable_range((void*)world,0x2000)){
                mgr=get_manager(world);player=get_player(world);
                if(mgr&&player)break;
            }
        }
        Sleep(500);
    }
    if(!world||!mgr||!player){log_line("[RESPAWN] Player/manager did not become ready within 10 seconds; cycle aborted safely.");return 0;}

    if(apply_native_warp_assist(mgr))
        log_line("[WARP] Native catch-up enabled: 15m distance / 0.75s blocked ray or stuck path.");
    else
        log_line("[WARP] Native warp manager not ready yet; monitor will retry after the stable transition window.");

    log_line("[HANDSHAKE] Pre-arming SummonBuddyManager for 1500 ms.");
    for(int i=0;i<6;++i){
        if(!readable_range((void*)mgr,0xC0)){log_line("[HANDSHAKE] Manager became unreadable; cycle aborted safely.");return 0;}
        force_buddy_available(mgr);Sleep(250);
    }

    request_companion(world,mgr);
    log_line("[HANDSHAKE] Request submitted; holding safe manager fields for 3000 ms.");
    int goods=0,speffect=0;get_companion_ids(&goods,&speffect);
    for(int i=0;i<12;++i){
        if(!readable_range((void*)mgr,0xC0)){log_line("[HANDSHAKE] Manager became unreadable after request; stopping writes.");break;}
        force_buddy_available(mgr);
        *(volatile int*)(mgr+SBM_REQUESTED_GOODS)=goods;
        *(volatile float*)(mgr+SBM_ITEM_USE_COOLDOWN)=0.0f;
        *(volatile BYTE*)(mgr+SBM_DISAPPEAR_REQUESTED)=0;
        *(volatile int*)(mgr+SBM_REQUEST_SPEFFECT)=speffect;
        Sleep(250);
    }

    log_line("[FURY] Handshake complete. Waiting 1000 ms before bounded identity lookup.");
    Sleep(1000);
    ULONGLONG companion=0;
    for(int attempt=1;attempt<=6 && !companion;++attempt){
        if(readable_range(world_global,8)){
            world=*(volatile ULONGLONG*)world_global;
            if(world&&readable_range((void*)world,0x2000)){
                mgr=get_manager(world);
                if(mgr){companion=get_exact_companion_raw_only(world,mgr,expected_npc);if(!companion)companion=get_companion_vtable_once_no_vq(world,mgr,expected_npc);}
            }
        }
        if(!companion)Sleep(500);
    }
    if(!companion){log_line("[RESPAWN] Summon cycle finished, but the selected companion was not captured by raw or one-shot native ChrSet lookup.");return 0;}

    int actual_npc=0;if(seh_read_i32((volatile int*)(companion+CHR_NPC_PARAM_ID),&actual_npc)&&actual_npc>0)g_cached_companion_npc=actual_npc;
    log_i32_line("[IDENTITY] SUCCESS: cached Spirit Ash NpcParam=",g_cached_companion_npc?g_cached_companion_npc:expected_npc);
    if(g_apply_speffect&&g_remove_speffect){
        BOOL ok=apply_safe_fury_once(companion);
        log_line(ok?"[FURY] ONE-SHOT transition completed successfully.":"[FURY] ONE-SHOT transition did not verify as successful.");
    }else log_line("[FURY] Skipped because apply/remove functions were not resolved.");
    return companion;
}

static DWORD __stdcall worker_thread(LPVOID){
    build_sibling_path(g_ini_path,1024,L"SpiritAshCompanion_Reforged.ini");
    build_sibling_path(g_log_path,1024,L"SpiritAshCompanion_Reforged.log");
    load_config();
    g_cfg.enable_real_item_fury=0;
    g_cfg.enable_debug_npc_probe=0;

    log_line("============================================================");
    log_line("[LOAD] SpiritAshCompanion Reforged v3.8.5 FOUR PRESETS + DIRECT CATCH-UP FALLBACK loaded.");
    log_line("[SAFE] Player ChrIns internals are never dereferenced by the death guard.");
    log_line("[SAFE] Any world/player/manager pointer transition enters a 4s quiet window with ZERO summon/Fury calls.");
    log_line("[SAFE] Selected companion identity acquisition uses raw reads plus one-shot ChrSet::safe_get fallback; no per-entry VirtualQuery stalls.");
    log_line("[SAFE] Fury keeps the proven cached 22s rearm and never scans ChrSet/SpEffect while healthy.");
