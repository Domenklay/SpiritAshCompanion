                    }
                }
            }
            continue;
        }

        // During a real boss-fog/native warp, SummonBuddyManager::player_has_alive can legitimately
        // drop to zero while the group is being detached/warped/faded back in. Never interpret that
        // temporary state as death; otherwise the respawn handshake cancels/restarts the native warp.
        if(gap_catchup_pending || post_transition_warp_pending || now<warp_respawn_suppress_until){
            missing_since=0;
            if(!warp_respawn_suppress_logged){
                log_line("[RESPAWN] Catch-up recovery active; alive=0 is temporarily ignored and no summon request will be sent.");
                warp_respawn_suppress_logged=TRUE;
            }
            continue;
        }

        // Stable no-hitch path: the manager alive flag is known to flicker/lie in Reforged.
        // If we already cached the exact selected companion ChrIns, verify only that companion under SEH.  We do
        // NOT dereference player ChrIns here.  This prevents a false alive=0 from launching a heavy
        // summon handshake every few seconds while the companion is actually still present.
        if(companion){
            int cached_npc=0;DWORD cached_state=0;
            int cached_expected=g_cached_companion_npc?g_cached_companion_npc:expected_npc;
            BOOL npc_ok=seh_read_i32((volatile int*)(companion+CHR_NPC_PARAM_ID),&cached_npc)&&cached_npc==cached_expected;
            BOOL state_ok=seh_read_u32((volatile DWORD*)(companion+CHR_LOAD_STATE),&cached_state);
            if(npc_ok&&state_ok&&!(cached_state&(1u<<7))){
                missing_since=0;
                continue;
            }
            companion=0;
        }

        // Do not immediately interpret alive=0 as companion death. During player death the manager
        // can report zero before the player/world pointers change. Requiring three seconds of a fully
        // stable pointer set prevents a summon handshake from starting inside that dangerous window.
        if(!missing_since){missing_since=now;continue;}
        ULONGLONG required_missing=post_transition?1000ull:3000ull;
        if(now-missing_since<required_missing)continue;
        if(now-last_attempt<RESPAWN_REQUEST_COOLDOWN_MS)continue;

        BOOL was_post_transition=post_transition;
        last_attempt=now;missing_since=0;companion=0;post_transition=FALSE;
        log_line(was_post_transition?"[RESPAWN] Post-transition companion absence confirmed; starting one bounded respawn handshake.":"[RESPAWN] Stable companion absence confirmed for 3s; starting one bounded respawn handshake.");
        ULONGLONG fresh=run_safe_summon_cycle(world_global,expected_npc,"[RESPAWN] Respawn cycle starting.");
        if(fresh){
            companion=fresh;companion_seen_once=TRUE;last_fury_rearm=GetTickCount64();fury_rearm_logged=FALSE;
            // The handshake can legitimately mutate manager internals. Re-enter a short quiet/cache
            // refresh phase instead of immediately trusting old addresses.
            cached_mgr=0;cached_alive=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
            // Internal cache refresh only: do not arm boss-fog warp on the next pointer acquisition.
            post_transition_warp_pending=FALSE;post_transition_group_request_logged=FALSE;gap_catchup_pending=FALSE;gap_catchup_deadline=0;warp_respawn_suppress_until=0;warp_respawn_suppress_logged=FALSE;
            quiet_until=GetTickCount64()+1500ull;
            log_line("[RESPAWN] SUCCESS: new companion cached; cache refresh will NOT be treated as a boss-fog transition.");
        }else{
            cached_mgr=0;cached_alive=0;post_transition=TRUE;identity_checked_after_transition=FALSE;
            quiet_until=GetTickCount64()+3000ull;
            log_line("[RESPAWN] Respawn cycle did not produce companion; retry remains cooldown-bounded.");
        }
    }
}

extern "C" __declspec(dllexport) BOOL __stdcall DllMain(HMODULE hModule,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){g_self=hModule;DisableThreadLibraryCalls(hModule);HANDLE t=CreateThread(0,0,worker_thread,0,0,0);if(t)CloseHandle(t);}return TRUE;}
