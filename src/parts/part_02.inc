static const ULONGLONG GAP_CATCHUP_WINDOW_MS      = 16000ull;
static const ULONGLONG SBM_CHR_SET               = 0x30ull;
static const ULONGLONG SBM_ITEM_USE_COOLDOWN     = 0x90ull;
static const ULONGLONG SBM_SPAWN_ORIGIN          = 0xA0ull;
static const ULONGLONG SBM_SPAWN_ROTATION        = 0xB0ull;

#define PAGE_READWRITE 0x04u
#define PAGE_WRITECOPY 0x08u
#define PAGE_EXECUTE_WRITECOPY 0x80u
#define MEM_PRIVATE 0x00020000u

struct Vec4 { float x,y,z,w; };

static void load_config(){
    g_cfg.start_delay_ms=GetPrivateProfileIntW(L"Companion",L"StartDelayMs",8000,g_ini_path);
    g_cfg.poll_ms=GetPrivateProfileIntW(L"Companion",L"PollMs",100,g_ini_path);
    g_cfg.respawn_delay_ms=GetPrivateProfileIntW(L"Companion",L"RespawnDelayMs",2500,g_ini_path);
    g_cfg.request_cooldown_ms=GetPrivateProfileIntW(L"Companion",L"RequestCooldownMs",6000,g_ini_path);
    g_cfg.enable_summon_anywhere=GetPrivateProfileIntW(L"Companion",L"EnableSummonAnywhere",1,g_ini_path)?1:0;
    g_cfg.force_buddy_activation=GetPrivateProfileIntW(L"Companion",L"ForceBuddyActivation",1,g_ini_path)?1:0;
    g_cfg.prevent_manager_disappear=GetPrivateProfileIntW(L"Companion",L"PreventManagerDisappear",1,g_ini_path)?1:0;
    g_cfg.force_spirit_fury=GetPrivateProfileIntW(L"Companion",L"ForceSpiritFury",1,g_ini_path)?1:0;
    g_cfg.fury_delay_ms=GetPrivateProfileIntW(L"Companion",L"FuryDelayMs",1200,g_ini_path);
    g_cfg.fury_refresh_ms=GetPrivateProfileIntW(L"Companion",L"FuryRefreshMs",2000,g_ini_path);
    g_cfg.enable_real_item_fury=GetPrivateProfileIntW(L"Companion",L"EnableRealItemFuryPulse",0,g_ini_path)?1:0;
    g_cfg.real_item_fury_delay_ms=GetPrivateProfileIntW(L"Companion",L"RealItemFuryDelayMs",3500,g_ini_path);
    g_cfg.real_item_pulse_ms=GetPrivateProfileIntW(L"Companion",L"RealItemPulseMs",700,g_ini_path);
    // Presets: 0 Finlay, 1 Jarwight, 2 Mimic Tear, 3 Tarnished (Nepheli Loux).
    g_cfg.preset=(int)GetPrivateProfileIntW(L"Companion",L"Preset",0,g_ini_path);
    if(g_cfg.preset<0||g_cfg.preset>3)g_cfg.preset=0;
    g_cfg.enable_debug_npc_probe=GetPrivateProfileIntW(L"DebugNPC",L"Enabled",1,g_ini_path)?1:0;
    g_cfg.debug_npc_delay_ms=GetPrivateProfileIntW(L"DebugNPC",L"DelayMs",1800,g_ini_path);
    g_cfg.debug_chr_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"ChrId",0,g_ini_path);
    g_cfg.debug_chara_init_param_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"CharaInitParamId",23640,g_ini_path);
    g_cfg.debug_npc_param_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"NpcParamId",100000050,g_ini_path);
    g_cfg.debug_npc_think_param_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"NpcThinkParamId",100000050,g_ini_path);
    g_cfg.debug_event_entity_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"EventEntityId",0,g_ini_path);
    g_cfg.debug_talk_id=(int)GetPrivateProfileIntW(L"DebugNPC",L"TalkId",0,g_ini_path);
    g_cfg.debug_offset_x_cm=(int)GetPrivateProfileIntW(L"DebugNPC",L"OffsetXcm",250,g_ini_path);
    if(g_cfg.debug_npc_delay_ms<500)g_cfg.debug_npc_delay_ms=500;if(g_cfg.debug_npc_delay_ms>30000)g_cfg.debug_npc_delay_ms=30000;
    if(g_cfg.debug_offset_x_cm<-1000)g_cfg.debug_offset_x_cm=-1000;if(g_cfg.debug_offset_x_cm>1000)g_cfg.debug_offset_x_cm=1000;
    if(g_cfg.poll_ms<10)g_cfg.poll_ms=10;if(g_cfg.poll_ms>1000)g_cfg.poll_ms=1000;
    if(g_cfg.respawn_delay_ms<250)g_cfg.respawn_delay_ms=250;if(g_cfg.respawn_delay_ms>30000)g_cfg.respawn_delay_ms=30000;
    if(g_cfg.request_cooldown_ms<1000)g_cfg.request_cooldown_ms=1000;if(g_cfg.request_cooldown_ms>60000)g_cfg.request_cooldown_ms=60000;
    if(g_cfg.fury_delay_ms<300)g_cfg.fury_delay_ms=300;if(g_cfg.fury_delay_ms>15000)g_cfg.fury_delay_ms=15000;
    if(g_cfg.fury_refresh_ms<500)g_cfg.fury_refresh_ms=500;if(g_cfg.fury_refresh_ms>10000)g_cfg.fury_refresh_ms=10000;
    if(g_cfg.real_item_fury_delay_ms<1200)g_cfg.real_item_fury_delay_ms=1200;if(g_cfg.real_item_fury_delay_ms>15000)g_cfg.real_item_fury_delay_ms=15000;
    if(g_cfg.real_item_pulse_ms<150)g_cfg.real_item_pulse_ms=150;if(g_cfg.real_item_pulse_ms>1500)g_cfg.real_item_pulse_ms=1500;
}

static ULONGLONG get_player(ULONGLONG world){
    if(!world||!readable_range((void*)(world+WCM_MAIN_PLAYER),8))return 0;
    ULONGLONG p=*(volatile ULONGLONG*)(world+WCM_MAIN_PLAYER);
    return(p&&readable_range((void*)p,0x200))?p:0;
}
static ULONGLONG get_manager(ULONGLONG world){
    if(!world||!readable_range((void*)(world+WCM_SUMMON_BUDDY_MANAGER),8))return 0;
    ULONGLONG m=*(volatile ULONGLONG*)(world+WCM_SUMMON_BUDDY_MANAGER);
    if(!m||!readable_range((void*)m,SBM_SIZE))return 0;
    ULONGLONG set=*(volatile ULONGLONG*)(m+SBM_CHR_SET);
    if(!set)return 0;
    return m;
}

// v3.8.2: use Elden Ring's own SummonBuddyWarpManager instead of writing ChrIns position.
// Normal catch-up lowers the engine's distance/ray/path-stuck thresholds.  Boss-fog catch-up below
// requests the native RequestWarp stage once after a pointer transition.  No physics position is written.
static BOOL apply_native_warp_assist(ULONGLONG mgr){
    if(!mgr)return FALSE;
    ULONGLONG warp=0;
    if(!seh_read_u64((volatile ULONGLONG*)(mgr+SBM_WARP_MANAGER),&warp)||!warp)return FALSE;
    BOOL a=seh_write_f32((volatile float*)(warp+BWM_TRIGGER_RAY_BLOCK),WARP_ASSIST_RAY_BLOCK_SEC);
    BOOL b=seh_write_f32((volatile float*)(warp+BWM_TRIGGER_DIST_PLAYER),WARP_ASSIST_DISTANCE);
    BOOL c=seh_write_f32((volatile float*)(warp+BWM_TRIGGER_PATH_TIME),WARP_ASSIST_PATH_TIME_SEC);
    BOOL d=seh_write_f32((volatile float*)(warp+BWM_TRIGGER_PATH_RANGE),WARP_ASSIST_PATH_RANGE);
    return a&&b&&c&&d;
}

// Request warp at the SummonBuddyGroup level.  This is the documented trigger that precedes
// creation/use of SummonBuddyWarpManager entries: SummonBuddyGroup::warp_requested.
// Called only in the short post-transition boss-fog window; no continuous tree/list walk runs in healthy gameplay.
static int request_native_buddy_group_warp_once(ULONGLONG mgr){
    if(!mgr)return 0;
    ULONGLONG map=mgr+SBM_GROUPS,head=0,size64=0,root=0;
    if(!seh_read_u64((volatile ULONGLONG*)(map+TREE_HEAD),&head)||!head)return 0;
    if(!seh_read_u64((volatile ULONGLONG*)(map+TREE_SIZE),&size64)||!size64||size64>32ull)return 0;
    if(!seh_read_u64((volatile ULONGLONG*)(head+0x08ull),&root)||!root||root==head)return 0;

    ULONGLONG stack[64];ULONGLONG seen[64];int sp=0,seen_n=0,requested=0;
    stack[sp++]=root;
    while(sp>0&&seen_n<64){
        ULONGLONG node=stack[--sp];
        if(!node||node==head)continue;
        BOOL duplicate=FALSE;for(int i=0;i<seen_n;++i){if(seen[i]==node){duplicate=TRUE;break;}}
        if(duplicate)continue;seen[seen_n++]=node;

        BYTE is_nil=1;if(!seh_read_u8((volatile BYTE*)(node+RBN_IS_NIL),&is_nil)||is_nil)continue;

        ULONGLONG list=node+RBN_GROUP_LIST,list_head=0,list_len=0;
        if(seh_read_u64((volatile ULONGLONG*)(list+LIST_HEAD),&list_head)&&list_head&&
           seh_read_u64((volatile ULONGLONG*)(list+LIST_LENGTH),&list_len)&&list_len>0&&list_len<=16ull){
            ULONGLONG current=0;
            if(seh_read_u64((volatile ULONGLONG*)(list_head+LIST_NODE_NEXT),&current)){
                for(ULONGLONG i=0;i<list_len&&current&&current!=list_head;++i){
                    ULONGLONG chr=0;BYTE disappearing=1;
                    BOOL have_chr=seh_read_u64((volatile ULONGLONG*)(current+LIST_NODE_VALUE+GROUP_CHR_INS),&chr);
                    BOOL have_dis=seh_read_u8((volatile BYTE*)(current+LIST_NODE_VALUE+GROUP_DISAPPEAR_REQUESTED),&disappearing);
                    if(have_chr&&chr&&have_dis&&!disappearing){
                        if(seh_write_u8((volatile BYTE*)(current+LIST_NODE_VALUE+GROUP_WARP_REQUESTED),1))++requested;
                    }
                    ULONGLONG next=0;if(!seh_read_u64((volatile ULONGLONG*)(current+LIST_NODE_NEXT),&next))break;current=next;
                }
            }
        }

        ULONGLONG left=0,right=0;
        if(seh_read_u64((volatile ULONGLONG*)(node+RBN_LEFT),&left)&&left&&left!=head&&sp<64)stack[sp++]=left;
        if(seh_read_u64((volatile ULONGLONG*)(node+RBN_RIGHT),&right)&&right&&right!=head&&sp<64)stack[sp++]=right;
    }
    return requested;
}

static int native_warp_entry_count(ULONGLONG mgr){
    if(!mgr)return 0;
    ULONGLONG warp=0,size64=0;
    if(!seh_read_u64((volatile ULONGLONG*)(mgr+SBM_WARP_MANAGER),&warp)||!warp)return 0;
    if(!seh_read_u64((volatile ULONGLONG*)(warp+TREE_SIZE),&size64)||size64>32ull)return 0;
    return (int)size64;
}
static BOOL sane_float(float v){if(!(v==v))return FALSE;return(v>-100000000.0f&&v<100000000.0f)?TRUE:FALSE;}
static BOOL get_player_position(ULONGLONG player,Vec4* out){
    if(!player||!out||!readable_range((void*)(player+CHR_MODULES),8))return FALSE;
    ULONGLONG mods=*(volatile ULONGLONG*)(player+CHR_MODULES);if(!mods||!readable_range((void*)(mods+CHR_MODULES_PHYSICS),8))return FALSE;
    ULONGLONG phy=*(volatile ULONGLONG*)(mods+CHR_MODULES_PHYSICS);if(!phy||!readable_range((void*)(phy+PHYSICS_POSITION),sizeof(Vec4)))return FALSE;
    volatile float* p=(volatile float*)(phy+PHYSICS_POSITION);Vec4 v={p[0],p[1],p[2],p[3]};
    if(!sane_float(v.x)||!sane_float(v.y)||!sane_float(v.z))return FALSE;*out=v;return TRUE;
}

// v3.8.5: bounded catch-up fallback.  The current fromsoftware-rs CSChrPhysicsModule layout
// exposes position at +0x70, last_update_position at +0x80 and chr_proxy_pos_update_requested
// at +0x91.  We update only those documented physics fields, once per recovery/cooldown; no
// animation/model/chunk fields are written and no game function is called here.
static BOOL get_chr_physics_position_seh(ULONGLONG chr,Vec4* out,ULONGLONG* out_phy){
    if(!chr||!out)return FALSE;
    ULONGLONG mods=0,phy=0;
    if(!seh_read_u64((volatile ULONGLONG*)(chr+CHR_MODULES),&mods)||!mods)return FALSE;
    if(!seh_read_u64((volatile ULONGLONG*)(mods+CHR_MODULES_PHYSICS),&phy)||!phy)return FALSE;
    Vec4 v={};
    if(!seh_read_f32((volatile float*)(phy+PHYSICS_POSITION+0),&v.x)||
       !seh_read_f32((volatile float*)(phy+PHYSICS_POSITION+4),&v.y)||
       !seh_read_f32((volatile float*)(phy+PHYSICS_POSITION+8),&v.z)||
       !seh_read_f32((volatile float*)(phy+PHYSICS_POSITION+12),&v.w))return FALSE;
    if(!sane_float(v.x)||!sane_float(v.y)||!sane_float(v.z))return FALSE;
    *out=v;if(out_phy)*out_phy=phy;return TRUE;
}
static BOOL direct_sync_companion_to_player(ULONGLONG companion,ULONGLONG player){
    if(!companion||!player)return FALSE;
    int actual_npc=0;
    int expected=g_cached_companion_npc;
    if(!seh_read_i32((volatile int*)(companion+CHR_NPC_PARAM_ID),&actual_npc)||actual_npc<=0)return FALSE;
    if(expected>0&&actual_npc!=expected)return FALSE;
    Vec4 target={},cur={};ULONGLONG player_phy=0,buddy_phy=0;
    if(!get_chr_physics_position_seh(player,&target,&player_phy))return FALSE;
    if(!get_chr_physics_position_seh(companion,&cur,&buddy_phy)||!buddy_phy)return FALSE;
    BOOL ok=TRUE;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_POSITION+0),target.x)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_POSITION+4),target.y)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_POSITION+8),target.z)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_POSITION+12),target.w)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_LAST_UPDATE_POS+0),target.x)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_LAST_UPDATE_POS+4),target.y)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_LAST_UPDATE_POS+8),target.z)&&ok;
    ok=seh_write_f32((volatile float*)(buddy_phy+PHYSICS_LAST_UPDATE_POS+12),target.w)&&ok;
    ok=seh_write_u8((volatile BYTE*)(buddy_phy+PHYSICS_PROXY_POS_REQUEST),1)&&ok;
    return ok;
}
static float distance_sq3(const Vec4& a,const Vec4& b){
    float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;return dx*dx+dy*dy+dz*dz;
}
static void force_buddy_available(ULONGLONG mgr){
    if(!mgr)return;
    if(g_cfg.force_buddy_activation){*(volatile BYTE*)(mgr+SBM_ACTIVATION)=1;*(volatile BYTE*)(mgr+SBM_PREV_ACTIVATION)=1;*(volatile BYTE*)(mgr+SBM_WARN)=1;*(volatile BYTE*)(mgr+SBM_PREV_WARN)=1;}
    if(g_cfg.prevent_manager_disappear)*(volatile BYTE*)(mgr+SBM_DISAPPEAR_REQUESTED)=0;
}

static void get_companion_ids(int* goods,int* speffect){
    // User-facing stable preset order:
    // 0 Finlay +10, 1 Jarwight Puppet +10, 2 Mimic Tear +10, 3 Tarnished (Nepheli Loux Puppet) +10.
    int g=223010,s=223000;
    if(g_cfg.preset==0){g=223010;s=223000;}
    else if(g_cfg.preset==1){g=263010;s=263000;}
    else if(g_cfg.preset==2){g=207010;s=207000;}
    else if(g_cfg.preset==3){g=259010;s=259000;}
    *goods=g;*speffect=s;
}
static int get_expected_companion_npc_param(){
    // Exact Spirit Summon NpcParam IDs. The one-shot non-Torrent fallback remains available
    // if Reforged substitutes a runtime variant, then caches that actual NpcParam.
    if(g_cfg.preset==0)return 223000;      // Cleanrot Knight Finlay
    if(g_cfg.preset==1)return 100000060;  // Jarwight Puppet
