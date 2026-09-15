static DeferredParamScan g_param_scan={};

static void start_deferred_param_scan(){
    if(g_param_scan.active||g_param_scan.finished)return;
    g_param_scan.cursor=0x10000ull;
    g_param_scan.region_base=0;
    g_param_scan.region_size=0;
    g_param_scan.region_offset=0;
    g_param_scan.bytes_scanned=0;
    g_param_scan.buddy_row=0;
    g_param_scan.melina_npc_row=0;
    g_param_scan.melina_think_row=0;
    g_param_scan.active=TRUE;
    g_param_scan.finished=FALSE;
    g_param_scan.patched=FALSE;
    g_param_scan.progress_512_logged=FALSE;
    log_line("[PARAM-SCAN] Deferred bounded scan started AFTER Dolores is alive; max 2 MiB per pass.");
}

static BOOL writable_region(const MEMORY_BASIC_INFORMATION_X64* mbi){
    if(!mbi||mbi->State!=MEM_COMMIT)return FALSE;
    if((mbi->Protect&PAGE_GUARD)||(mbi->Protect&PAGE_NOACCESS))return FALSE;
    DWORD p=mbi->Protect&0xFFu;
    return (p==0x04u||p==0x08u||p==PAGE_EXECUTE_READWRITE||p==0x80u)?TRUE:FALSE;
}

static void patch_found_runtime_rows(){
    if(g_param_scan.patched)return;
    if(!g_param_scan.buddy_row||!g_param_scan.melina_npc_row||!g_param_scan.melina_think_row)return;

    BYTE* b=(BYTE*)g_param_scan.buddy_row;
    BYTE* n=(BYTE*)g_param_scan.melina_npc_row;
    BYTE* t=(BYTE*)g_param_scan.melina_think_row;
    if(!readable_range(b,160)||!readable_range(n,736)||!readable_range(t,228))return;

    // Prepare Melina BEFORE the Buddy pipeline creates her.
    // These offsets are derived directly from the user's params:
    // every referenced Spirit Ash NpcParam has TeamType 47 at +0x133;
    // every referenced buddy NpcThink has isBuddyAI=1 at +0x42.
    n[0x133]=47;
    t[0x42]=1;

    // Redirect the already-working Dolores BuddyParam row to Melina.
    *(volatile int*)(b+0x08)=21800000;
    *(volatile int*)(b+0x0C)=21800000;
    *(volatile int*)(b+0x10)=-1;
    *(volatile int*)(b+0x14)=-1;
    *(volatile BYTE*)(b+0x24)=0;
    *(volatile BYTE*)(b+0x25)=0;
    *(volatile BYTE*)(b+0x26)=0;
    *(volatile int*)(b+0x54)=-1;
    *(volatile int*)(b+0x58)=-1;

    BOOL ok=
        *(volatile int*)(b+0x08)==21800000 &&
        *(volatile int*)(b+0x0C)==21800000 &&
        n[0x133]==47 &&
        t[0x42]==1;
    if(ok){
        g_param_scan.patched=TRUE;
        log_line("[PARAM-PATCH] SUCCESS: live Dolores BuddyParam now targets Melina; Melina TeamType=47 and isBuddyAI=1 BEFORE respawn.");
    }else{
        log_line("[PARAM-PATCH] WARNING: live-row writes did not persist; keeping current Dolores and not forcing a respawn.");
    }
}

static void deferred_param_scan_step(SIZE_T budget){
    if(!g_param_scan.active||g_param_scan.finished||!budget)return;
    const SIZE_T MAX_ROW=736;
    while(budget>0 && g_param_scan.active){
        if(!g_param_scan.region_base || g_param_scan.region_offset>=g_param_scan.region_size){
            MEMORY_BASIC_INFORMATION_X64 mbi;
            if(!VirtualQuery((void*)g_param_scan.cursor,&mbi,sizeof(mbi))){
                g_param_scan.active=FALSE;g_param_scan.finished=TRUE;
                log_line("[PARAM-SCAN] Address-space scan finished; required live rows were not all found.");
                return;
            }
            ULONGLONG base=(ULONGLONG)mbi.BaseAddress;
            ULONGLONG next=base+(ULONGLONG)mbi.RegionSize;
            if(next<=g_param_scan.cursor){
                g_param_scan.active=FALSE;g_param_scan.finished=TRUE;return;
            }
            g_param_scan.cursor=next;
            if(!writable_region(&mbi)||mbi.RegionSize<64){
                g_param_scan.region_base=0;g_param_scan.region_size=0;g_param_scan.region_offset=0;
                continue;
            }
            g_param_scan.region_base=base;
            g_param_scan.region_size=mbi.RegionSize;
            g_param_scan.region_offset=0;
        }

        SIZE_T remain=g_param_scan.region_size-g_param_scan.region_offset;
        SIZE_T take=remain<budget?remain:budget;
        BYTE* region=(BYTE*)g_param_scan.region_base;
        BYTE* start=region+g_param_scan.region_offset;
        BYTE* limit=start+take;
        BYTE* region_end=region+g_param_scan.region_size;

        // Align to 4-byte boundaries. Full verification never crosses the readable region.
        ULONGLONG sa=(ULONGLONG)start;
        sa=(sa+3ull)&~3ull;
        BYTE* q=(BYTE*)sa;
        for(;q<limit;q+=4){
            SIZE_T avail=(SIZE_T)(region_end-q);

            if(!g_param_scan.buddy_row && avail>=160){
                if(*(volatile DWORD*)(q+4)==262000u &&
                   *(volatile int*)(q+8)==100000050 &&
                   *(volatile int*)(q+12)==100000050 &&
                   bytes_equal_160(q,DOLORES_BUDDY_ROW_ORIGINAL)){
                    g_param_scan.buddy_row=(ULONGLONG)q;
                    log_line("[PARAM-SCAN] Found exact live Dolores BuddyParam 26200000 row.");
                }
            }

            if(!g_param_scan.melina_npc_row && avail>=736){
                if(*(volatile DWORD*)(q+4)==21800u &&
                   *(volatile DWORD*)(q+8)==11u &&
                   bytes_equal_n(q,MELINA_NPC_PREFIX,64) &&
                   bytes_equal_n(q+0x140,MELINA_NPC_MARKER_140,32)){
                    g_param_scan.melina_npc_row=(ULONGLONG)q;
                    log_line("[PARAM-SCAN] Found exact live Melina NpcParam 21800000 row.");
                }
            }

            if(!g_param_scan.melina_think_row && avail>=228){
                if(*(volatile DWORD*)(q+4)==30000u &&
                   *(volatile DWORD*)(q+8)==218000u &&
                   bytes_equal_n(q,MELINA_THINK_PREFIX,64)){
                    g_param_scan.melina_think_row=(ULONGLONG)q;
                    log_line("[PARAM-SCAN] Found exact live Melina NpcThinkParam 21800000 row.");
                }
            }

            if(g_param_scan.buddy_row&&g_param_scan.melina_npc_row&&g_param_scan.melina_think_row){
                patch_found_runtime_rows();
                g_param_scan.active=FALSE;g_param_scan.finished=TRUE;
                return;
            }
        }

        g_param_scan.region_offset+=take;
        g_param_scan.bytes_scanned+=take;
        if(!g_param_scan.progress_512_logged && g_param_scan.bytes_scanned>=0x20000000ull){
            g_param_scan.progress_512_logged=TRUE;
            log_line("[PARAM-SCAN] 512 MiB checked incrementally; Dolores remains active while search continues.");
        }
        budget-=take;
    }
}

static void prepare_substituted_buddy(ULONGLONG chr){
    if(!chr||!readable_range((void*)chr,0x180))return;
    int npc=*(volatile int*)(chr+CHR_NPC_PARAM_ID);
    BYTE old_team=*(volatile BYTE*)(chr+CHR_TEAM_TYPE);
    // TeamType 47 is the game's Spirit Summon team. Unlike v1.7 we do NOT touch ChrType.
    *(volatile BYTE*)(chr+CHR_TEAM_TYPE)=47;
    if(g_apply_speffect)g_apply_speffect((void*)chr,297000,TRUE); // common buddy-follow/AI helper used by custom spirit mods.
    if(npc==21800000)
        log_line("[NPC-SUBSTITUTE] SUCCESS: Buddy pipeline produced Melina NPCParam 21800000. TeamType forced to 47 and SpEffect 297000 applied.");
    else if(npc==100000050)
        log_line("[NPC-SUBSTITUTE] Buddy is still Dolores (NpcParam 100000050): SummonBuddyManager cached the old BuddyParam before our RAM patch.");
    else
        log_line("[NPC-SUBSTITUTE] Buddy spawned, but NpcParam is neither Melina nor Dolores; see next build for expanded diagnostics.");
    (void)old_team;
}

static void request_companion(ULONGLONG world,ULONGLONG mgr){
    ULONGLONG player=get_player(world);Vec4 p;
    if(player&&get_player_position(player,&p)){volatile float* dst=(volatile float*)(mgr+SBM_SPAWN_ORIGIN);dst[0]=p.x;dst[1]=p.y;dst[2]=p.z;dst[3]=p.w;*(volatile float*)(mgr+SBM_SPAWN_ROTATION)=0.0f;log_line("[SPAWN] Player position copied into summon spawn_origin.");}
    int goods=0,speffect=0;get_companion_ids(&goods,&speffect);
    *(volatile int*)(mgr+SBM_REQUESTED_GOODS)=goods;
    *(volatile float*)(mgr+SBM_ITEM_USE_COOLDOWN)=0.0f;
    *(volatile BYTE*)(mgr+SBM_DISAPPEAR_REQUESTED)=0;
    force_buddy_available(mgr);
    *(volatile int*)(mgr+SBM_REQUEST_SPEFFECT)=speffect;
    log_line("[SPAWN] Spirit Ash summon request sent.");
}

static ULONGLONG get_debug_chr_creator(ULONGLONG world){
    if(!world||!readable_range((void*)(world+WCM_DEBUG_CHR_CREATOR),8))return 0;
    ULONGLONG c=*(volatile ULONGLONG*)(world+WCM_DEBUG_CHR_CREATOR);
    return(c&&readable_range((void*)c,0x1C0))?c:0;
}

static void build_debug_chr_name(wchar_t out[32],int chr_id){
    for(int i=0;i<32;++i)out[i]=0;
    out[0]=L'c';
    int id=chr_id;if(id<0)id=-id;
    // The game's helper formats c####. Keep at least four digits, allow larger IDs too.
    wchar_t rev[12];int n=0;
    do{rev[n++]=(wchar_t)(L'0'+(id%10));id/=10;}while(id&&n<10);
    while(n<4&&n<10)rev[n++]=L'0';
    int pos=1;for(int i=n-1;i>=0&&pos<31;--i)out[pos++]=rev[i];
}

static BOOL request_debug_npc_probe(ULONGLONG world,ULONGLONG player){
    ULONGLONG creator=get_debug_chr_creator(world);
    if(!creator){log_line("[NPC] CSDebugChrCreator is not available yet.");return FALSE;}
