    log_line("[SAFE] Native warp thresholds remain enabled; boss-fog/distance fallback uses one-shot CSChrPhysicsModule synchronization only.");
    Sleep(g_cfg.start_delay_ms);

    if(g_cfg.enable_summon_anywhere){
        BOOL a=install_summon_anywhere_patch1(),b=install_summon_anywhere_patch2();
        log_line(a?"[PATCH] Summon-anywhere patch 1 installed.":"[PATCH] Summon-anywhere patch 1 NOT found.");
        log_line(b?"[PATCH] Summon-anywhere patch 2 installed.":"[PATCH] Summon-anywhere patch 2 NOT found.");
    }
    g_apply_speffect=scan_apply_speffect();
    g_remove_speffect=scan_remove_speffect();
    log_line(g_apply_speffect?"[FURY] ChrIns::apply_speffect found.":"[FURY] apply_speffect not found; Fury will be skipped.");
    log_line(g_remove_speffect?"[FURY] ChrIns::remove_speffect found.":"[FURY] remove_speffect not found; Fury will be skipped.");

    BYTE* world_global=scan_worldchrman_global_modern();
    if(!world_global){log_line("[ERROR] WorldChrMan not found. DLL will remain idle instead of rescanning.");for(;;)Sleep(60000);}
    int expected_npc=get_expected_companion_npc_param();
    log_preset();

    ULONGLONG companion=run_safe_summon_cycle(world_global,expected_npc,"[RESPAWN] Initial summon cycle starting.");
    ULONGLONG last_attempt=GetTickCount64();
    ULONGLONG last_fury_rearm=last_attempt;
    BOOL fury_rearm_logged=FALSE;

    // v3.7 death handling intentionally does NOT read CHR_LOAD_STATE or any other player-owned field.
    // During normal play we only compare already-known world/player/manager pointer values and read
    // the cached manager alive byte. Pointer changes are treated as a transition and create a quiet
    // period in which no game function is called and no manager field is written.
    ULONGLONG cached_world=0,cached_player=0,cached_mgr=0;
    volatile BYTE* cached_alive=0;
    ULONGLONG quiet_until=GetTickCount64()+4000ull;
    ULONGLONG missing_since=0;
    BOOL transition_logged=FALSE;
    BOOL post_transition=TRUE;
    BOOL identity_checked_after_transition=FALSE;
    BOOL warp_assist_applied=FALSE;
    BOOL warp_assist_warned=FALSE;
    ULONGLONG last_warp_attempt=0;
    BOOL companion_seen_once=companion?TRUE:FALSE;
    BOOL post_transition_warp_pending=FALSE;
    BOOL post_transition_group_request_logged=FALSE;
    ULONGLONG post_transition_warp_deadline=0;
    ULONGLONG warp_respawn_suppress_until=0;
    BOOL warp_respawn_suppress_logged=FALSE;
    BOOL gap_catchup_pending=FALSE;
    ULONGLONG gap_catchup_deadline=0;
    ULONGLONG last_gap_lookup=0;
    ULONGLONG last_direct_catchup=0;
    ULONGLONG last_distance_check=0;

    log_line("[RESPAWN] v3.8.5 monitor armed. Stable cached Fury + native warp thresholds + bounded direct catch-up fallback.");

    for(;;){
        Sleep(RESPAWN_MONITOR_MS);
        ULONGLONG now=GetTickCount64();

        ULONGLONG world=0;
        if(!seh_read_u64((volatile ULONGLONG*)world_global,&world)||!world){
            if(companion_seen_once){gap_catchup_pending=TRUE;gap_catchup_deadline=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_until=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_logged=FALSE;}
            cached_world=0;cached_player=0;cached_mgr=0;cached_alive=0;
            warp_assist_applied=FALSE;warp_assist_warned=FALSE;last_warp_attempt=0;
            companion=0;missing_since=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
            quiet_until=now+4000ull;
            if(!transition_logged){log_line("[TRANSITION] World unavailable; all summon/Fury work paused.");transition_logged=TRUE;}
            continue;
        }

        ULONGLONG player_raw=0,mgr_raw=0;
        BOOL have_player=seh_read_u64((volatile ULONGLONG*)(world+WCM_MAIN_PLAYER),&player_raw)&&player_raw;
        BOOL have_mgr=seh_read_u64((volatile ULONGLONG*)(world+WCM_SUMMON_BUDDY_MANAGER),&mgr_raw)&&mgr_raw;
        if(!have_player||!have_mgr){
            if(companion_seen_once){gap_catchup_pending=TRUE;gap_catchup_deadline=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_until=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_logged=FALSE;}
            cached_world=world;cached_player=have_player?player_raw:0;cached_mgr=0;cached_alive=0;
            warp_assist_applied=FALSE;warp_assist_warned=FALSE;last_warp_attempt=0;
            companion=0;missing_since=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
            quiet_until=now+4000ull;
            if(!transition_logged){log_line("[TRANSITION] Player/manager unavailable; all summon/Fury work paused.");transition_logged=TRUE;}
            continue;
        }

        BOOL pointer_changed=(world!=cached_world)||(player_raw!=cached_player)||(mgr_raw!=cached_mgr)||!cached_alive;
        if(pointer_changed){
            BOOL world_or_player_changed=(world!=cached_world)||(player_raw!=cached_player);
            // Validate manager only on an actual pointer transition. No player object dereference occurs.
            ULONGLONG mgr=get_manager(world);
            if(!mgr){
                if(companion_seen_once){gap_catchup_pending=TRUE;gap_catchup_deadline=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_until=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_logged=FALSE;}
                cached_world=world;cached_player=player_raw;cached_mgr=0;cached_alive=0;
                warp_assist_applied=FALSE;warp_assist_warned=FALSE;last_warp_attempt=0;
                companion=0;missing_since=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
                quiet_until=now+4000ull;
                if(!transition_logged){log_line("[TRANSITION] Manager not ready; all summon/Fury work paused.");transition_logged=TRUE;}
                continue;
            }
            // Only a real externally observed pointer replacement arms boss-fog warp.
            // A zero cached manager/alive pointer can be our own post-handshake cache refresh and must NOT
            // be mistaken for a fog transition, otherwise it creates a respawn->warp->respawn loop.
            BOOL real_external_transition=(cached_world&&world!=cached_world)||(cached_player&&player_raw!=cached_player)||(cached_mgr&&mgr_raw!=cached_mgr);
            if(companion_seen_once&&real_external_transition){
                gap_catchup_pending=TRUE;gap_catchup_deadline=now+GAP_CATCHUP_WINDOW_MS;
                warp_respawn_suppress_until=now+GAP_CATCHUP_WINDOW_MS;warp_respawn_suppress_logged=FALSE;
            }
            cached_world=world;cached_player=player_raw;cached_mgr=mgr;
            cached_alive=(volatile BYTE*)(mgr+SBM_PLAYER_HAS_ALIVE);
            warp_assist_applied=FALSE;warp_assist_warned=FALSE;last_warp_attempt=0;
            missing_since=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
            quiet_until=now+4000ull;
            if(world_or_player_changed)companion=0; // old ChrIns is invalid after player/world reconstruction.
            if(!transition_logged){log_line("[TRANSITION] World/player/manager changed; entering 4s quiet window.");transition_logged=TRUE;}
            continue;
        }

        // Critical death safety: after any pointer transition, do absolutely nothing until the
        // addresses have remained unchanged for four full seconds. This covers the death screen,
        // world teardown and respawn reconstruction without touching a dying player object.
        if(now<quiet_until)continue;
        if(transition_logged){log_line("[TRANSITION] Pointer set remained stable for 4s; normal monitoring resumed.");transition_logged=FALSE;}

        // The warp-manager pointer may become available slightly after SummonBuddyManager itself.
        // Retry only every 2 seconds until one successful patch, then do zero warp-manager work in healthy gameplay.
        if(!warp_assist_applied && (!last_warp_attempt || now-last_warp_attempt>=2000ull)){
            last_warp_attempt=now;
            if(apply_native_warp_assist(cached_mgr)){
                warp_assist_applied=TRUE;
                log_line("[WARP] Native warp assist active: 15m distance / 0.75s blocked ray or stuck path.");
            }else if(!warp_assist_warned){
                warp_assist_warned=TRUE;
                log_line("[WARP] Waiting for native SummonBuddyWarpManager; retry remains low-frequency.");
            }
        }

        // v3.8.5 boss-fog recovery: losing player/manager is itself the signal.  After the normal
        // four-second stability window, reacquire the existing buddy once (throttled) and synchronize
        // its CSChrPhysicsModule to the player's current position.  This bypasses the native warp-entry
        // creation path that Reforged did not create reliably at fog gates, while remaining one-shot.
        if(gap_catchup_pending){
            if(now>gap_catchup_deadline){
                gap_catchup_pending=FALSE;
                log_line("[WARP] Post-transition direct catch-up window expired without a recoverable companion.");
            }else if(!last_gap_lookup || now-last_gap_lookup>=750ull){
                last_gap_lookup=now;
                if(!companion){
                    ULONGLONG found=get_exact_companion_raw_only(world,cached_mgr,expected_npc);
                    if(!found)found=get_companion_vtable_once_no_vq(world,cached_mgr,expected_npc);
                    if(found){companion=found;companion_seen_once=TRUE;}
                }
                if(companion && (!last_direct_catchup || now-last_direct_catchup>=1500ull)){
                    if(direct_sync_companion_to_player(companion,cached_player)){
                        last_direct_catchup=now;gap_catchup_pending=FALSE;
                        ULONGLONG grace=now+3000ull;if(grace>warp_respawn_suppress_until)warp_respawn_suppress_until=grace;
                        log_line("[WARP] BOSS-FOG FALLBACK: existing companion physics synchronized to player; no resummon.");
                    }
                }
            }
        }

        BYTE alive_byte=0;
        if(!seh_read_u8(cached_alive,&alive_byte)){
            cached_mgr=0;cached_alive=0;warp_assist_applied=FALSE;warp_assist_warned=FALSE;last_warp_attempt=0;companion=0;missing_since=0;
            post_transition=TRUE;identity_checked_after_transition=FALSE;quiet_until=now+4000ull;
            continue;
        }

        if(alive_byte){
            missing_since=0;
            if(!gap_catchup_pending){warp_respawn_suppress_until=0;warp_respawn_suppress_logged=FALSE;}
            // If a loading transition preserved/auto-restored the Spirit Ash, reacquire its exact ChrIns
            // at most once after the quiet window. This is not repeated in healthy gameplay.
            if(post_transition&&!companion&&!identity_checked_after_transition){
                identity_checked_after_transition=TRUE;
                ULONGLONG found=get_exact_companion_raw_only(world,cached_mgr,expected_npc);if(!found)found=get_companion_vtable_once_no_vq(world,cached_mgr,expected_npc);
                if(found){
                    companion=found;companion_seen_once=TRUE;last_fury_rearm=now;fury_rearm_logged=FALSE;
                    log_line("[IDENTITY] Existing companion reacquired once after transition; no respawn request needed.");
                }
            }
            post_transition=FALSE;
            if(companion&&now-last_fury_rearm>=FURY_CACHED_REARM_MS){
                last_fury_rearm=now;
                if(rearm_cached_fury(companion,expected_npc)){
                    if(!fury_rearm_logged){
                        log_line("[FURY] Cached rearm executed successfully; future rearms stay silent to avoid log I/O.");
                        fury_rearm_logged=TRUE;
                    }
                }else{
                    companion=0;
                    log_line("[FURY] Cached companion pointer became stale; Fury rearm paused until the next real identity acquisition.");
                }
            }
            // Ordinary catch-up fallback: only once per second check two cached physics positions.
            // Native warp remains first-line behavior; direct sync is used only beyond 18m and has a 6s cooldown.
            if(companion && now-last_distance_check>=1000ull){
                last_distance_check=now;
                if(!last_direct_catchup || now-last_direct_catchup>=DIRECT_CATCHUP_COOLDOWN_MS){
                    Vec4 pp={},bp={};
                    if(get_chr_physics_position_seh(cached_player,&pp,0)&&get_chr_physics_position_seh(companion,&bp,0) &&
                       distance_sq3(pp,bp)>DIRECT_CATCHUP_DISTANCE*DIRECT_CATCHUP_DISTANCE){
                        if(direct_sync_companion_to_player(companion,cached_player)){
                            last_direct_catchup=now;
                            log_line("[WARP] DISTANCE FALLBACK: companion was over 18m away; one-shot physics sync completed.");
                        }
