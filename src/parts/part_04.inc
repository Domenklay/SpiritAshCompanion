
static RemoveSpEffectFn scan_remove_speffect(){
    // fromsoftware-rs mapper profile: BA 1C000000 48 8B CE 84 C0 74 ? E8 <remove_speffect>.
    static const BYTE pat[]={0xBA,0x1C,0x00,0x00,0x00,0x48,0x8B,0xCE,0x84,0xC0,0x74,0x00,0xE8};
    static const char mask[]="xxxxxxxxxxx?x";
    BYTE* p=scan_text(pat,mask,sizeof(pat));
    if(!p)return 0;
    BYTE* target=p+17+(LONGLONG)read_i32(p+13);
    ModuleInfo mi;SectionView text;if(!get_main_module_info(&mi)||!get_section(&mi,".text",&text))return 0;
    if(!ptr_in_section(target,&text))return 0;
    return (RemoveSpEffectFn)target;
}

static ULONGLONG find_speffect_entry_raw(ULONGLONG chr,int speffect){
    if(!chr||!readable_range((void*)(chr+CHR_SPECIAL_EFFECT),8))return 0;
    ULONGLONG sp=*(volatile ULONGLONG*)(chr+CHR_SPECIAL_EFFECT);
    if(!sp||!readable_range((void*)(sp+SPE_HEAD),8))return 0;
    ULONGLONG e=*(volatile ULONGLONG*)(sp+SPE_HEAD);
    for(int i=0;i<512 && e;++i){
        if(!readable_range((void*)e,0x38))return 0;
        if(*(volatile int*)(e+SPE_ENTRY_PARAM_ID)==speffect)return e;
        ULONGLONG next=*(volatile ULONGLONG*)(e+SPE_ENTRY_NEXT);
        if(next==e)break;
        e=next;
    }
    return 0;
}

static BOOL chr_has_speffect_raw(ULONGLONG chr,int speffect){
    return find_speffect_entry_raw(chr,speffect)?TRUE:FALSE;
}

static BOOL pin_speffect_entry_lifetime(ULONGLONG chr,int speffect,float seconds){
    ULONGLONG e=find_speffect_entry_raw(chr,speffect);
    if(!e||!readable_range((void*)e,0x50))return FALSE;
    if(!(seconds>1.0f && seconds<86400.0f))return FALSE;
    *(volatile float*)(e+SPE_ENTRY_REMOVAL_TIMER)=seconds;
    *(volatile float*)(e+SPE_ENTRY_DURATION)=seconds;
    float r=*(volatile float*)(e+SPE_ENTRY_REMOVAL_TIMER);
    float d=*(volatile float*)(e+SPE_ENTRY_DURATION);
    return (r>seconds-1.0f && d>seconds-1.0f)?TRUE:FALSE;
}

struct FuryEndurancePatchState{
    ULONGLONG row;
    float original_endurance;
    BOOL patched;
};
static FuryEndurancePatchState g_fury_endurance_patch={};

static BOOL make_fury_wrapper_permanent(ULONGLONG chr,int fury_wrapper){
    // This is intentionally a ONE-TIME linked-list lookup immediately after Fury is applied.
    // The live SpecialEffectEntry gives us its immutable SpEffectParam row; after that no healthy-state
    // SpEffect traversal or apply_speffect refresh is needed.
    ULONGLONG e=find_speffect_entry_raw(chr,fury_wrapper);
    if(!e||!readable_range((void*)(e+SPE_ENTRY_PARAM_DATA),8))return FALSE;
    ULONGLONG row=*(volatile ULONGLONG*)(e+SPE_ENTRY_PARAM_DATA);
    if(!row||!readable_range((void*)row,0x10))return FALSE;
    float old=*(volatile float*)(row+SEP_EFFECT_ENDURANCE);
    if(!(old>=-1.0f && old<10000.0f))return FALSE;
    if(!g_fury_endurance_patch.patched){
        g_fury_endurance_patch.row=row;
        g_fury_endurance_patch.original_endurance=old;
    }
    DWORD oldp=0;
    if(!VirtualProtect((void*)row,0x10,PAGE_EXECUTE_READWRITE,&oldp))return FALSE;
    *(volatile float*)(row+SEP_EFFECT_ENDURANCE)=-1.0f;
    DWORD ignored=0;VirtualProtect((void*)row,0x10,oldp,&ignored);
    float now=*(volatile float*)(row+SEP_EFFECT_ENDURANCE);
    if(now<-0.99f&&now>-1.01f){g_fury_endurance_patch.patched=TRUE;return TRUE;}
    return FALSE;
}

struct FuryParamPatchState{
    ULONGLONG row;
    float physics_attack_rate;
    float magic_attack_rate;
    float fire_attack_rate;
    float thunder_attack_rate;
    float target_priority;
    BOOL backed_up;
};
static FuryParamPatchState g_fury_param_patch={};

static BOOL sane_param_float(float v){return v>-100.0f && v<100.0f;}

static void restore_runtime_fury_param(){
    if(!g_fury_param_patch.backed_up||!g_fury_param_patch.row)return;
    ULONGLONG row=g_fury_param_patch.row;
    if(!readable_range((void*)row,0x100)){g_fury_param_patch.backed_up=FALSE;g_fury_param_patch.row=0;return;}
    DWORD oldp=0;
    if(VirtualProtect((void*)row,0x100,PAGE_EXECUTE_READWRITE,&oldp)){
        *(volatile float*)(row+SEP_PHYSICS_ATTACK_RATE)=g_fury_param_patch.physics_attack_rate;
        *(volatile float*)(row+SEP_MAGIC_ATTACK_RATE)=g_fury_param_patch.magic_attack_rate;
        *(volatile float*)(row+SEP_FIRE_ATTACK_RATE)=g_fury_param_patch.fire_attack_rate;
        *(volatile float*)(row+SEP_THUNDER_ATTACK_RATE)=g_fury_param_patch.thunder_attack_rate;
        *(volatile float*)(row+SEP_TARGET_PRIORITY)=g_fury_param_patch.target_priority;
        DWORD ignored=0;VirtualProtect((void*)row,0x100,oldp,&ignored);
    }
    g_fury_param_patch.backed_up=FALSE;g_fury_param_patch.row=0;
}

static BOOL patch_live_fury_param(ULONGLONG chr,int fury_body){
    ULONGLONG e=find_speffect_entry_raw(chr,fury_body);
    if(!e||!readable_range((void*)(e+SPE_ENTRY_PARAM_DATA),8)){
        log_line("[PARAM] Active Fury entry was not found; cannot resolve live SpEffectParam row yet.");
        return FALSE;
    }
    ULONGLONG row=*(volatile ULONGLONG*)(e+SPE_ENTRY_PARAM_DATA);
    if(!row||!readable_range((void*)row,0x100)){
        log_line("[PARAM] Fury entry exists, but its param_data pointer is not readable.");
        return FALSE;
    }
    float p=*(volatile float*)(row+SEP_PHYSICS_ATTACK_RATE);
    float m=*(volatile float*)(row+SEP_MAGIC_ATTACK_RATE);
    float f=*(volatile float*)(row+SEP_FIRE_ATTACK_RATE);
    float t=*(volatile float*)(row+SEP_THUNDER_ATTACK_RATE);
    float pr=*(volatile float*)(row+SEP_TARGET_PRIORITY);
    if(!sane_param_float(p)||!sane_param_float(m)||!sane_param_float(f)||!sane_param_float(t)||!sane_param_float(pr)){
        log_line("[PARAM] Refusing live-row patch because the resolved fields are not plausible floats.");
        return FALSE;
    }
    if(!g_fury_param_patch.backed_up||g_fury_param_patch.row!=row){
        if(g_fury_param_patch.backed_up&&g_fury_param_patch.row!=row)restore_runtime_fury_param();
        g_fury_param_patch.row=row;
        g_fury_param_patch.physics_attack_rate=p;
        g_fury_param_patch.magic_attack_rate=m;
        g_fury_param_patch.fire_attack_rate=f;
        g_fury_param_patch.thunder_attack_rate=t;
        g_fury_param_patch.target_priority=pr;
        g_fury_param_patch.backed_up=TRUE;
        log_line("[PARAM] Live 262200 Fury SpEffectParam row resolved through SpecialEffectEntry::param_data.");
    }
    DWORD oldp=0;
    if(!VirtualProtect((void*)row,0x100,PAGE_EXECUTE_READWRITE,&oldp)){
        log_line("[PARAM] VirtualProtect failed; live Fury row was not modified.");
        return FALSE;
    }
    *(volatile float*)(row+SEP_PHYSICS_ATTACK_RATE)=RUNTIME_FURY_DAMAGE_RATE;
    *(volatile float*)(row+SEP_MAGIC_ATTACK_RATE)=RUNTIME_FURY_DAMAGE_RATE;
    *(volatile float*)(row+SEP_FIRE_ATTACK_RATE)=RUNTIME_FURY_DAMAGE_RATE;
    *(volatile float*)(row+SEP_THUNDER_ATTACK_RATE)=RUNTIME_FURY_DAMAGE_RATE;
    *(volatile float*)(row+SEP_TARGET_PRIORITY)=RUNTIME_FURY_TARGET_PRIORITY;
    DWORD ignored=0;VirtualProtect((void*)row,0x100,oldp,&ignored);
    BOOL ok=
        *(volatile float*)(row+SEP_PHYSICS_ATTACK_RATE)==RUNTIME_FURY_DAMAGE_RATE &&
        *(volatile float*)(row+SEP_MAGIC_ATTACK_RATE)==RUNTIME_FURY_DAMAGE_RATE &&
        *(volatile float*)(row+SEP_FIRE_ATTACK_RATE)==RUNTIME_FURY_DAMAGE_RATE &&
        *(volatile float*)(row+SEP_THUNDER_ATTACK_RATE)==RUNTIME_FURY_DAMAGE_RATE &&
        *(volatile float*)(row+SEP_TARGET_PRIORITY)==RUNTIME_FURY_TARGET_PRIORITY;
    if(ok)log_line("[PARAM-VERIFY] Live Fury row now has x5 post-defense damage and targetPriority=1.0.");
    else log_line("[PARAM-VERIFY] WARNING: live Fury row write did not persist.");
    return ok;
}

struct RealItemPulseState{
    BOOL active;
    ULONGLONG started_at;
    ULONGLONG ar;
    DWORD queued_raw;
    DWORD saved_queued;
    ULONGLONG saved_action;
    ULONGLONG saved_prev;
    ULONGLONG saved_new;
    ULONGLONG saved_queued_inputs;
    ULONGLONG saved_possible;
};

static BOOL resolve_action_request(ULONGLONG player,ULONGLONG* out_ar){
    if(!player||!out_ar||!readable_range((void*)(player+CHR_MODULES),8))return FALSE;
    ULONGLONG modules=*(volatile ULONGLONG*)(player+CHR_MODULES);
    if(!modules||!readable_range((void*)(modules+MODULE_ACTION_REQUEST),8))return FALSE;
    ULONGLONG ar=*(volatile ULONGLONG*)(modules+MODULE_ACTION_REQUEST);
    if(!ar||!readable_range((void*)ar,0xA0))return FALSE;
    *out_ar=ar;
    return TRUE;
}

static BOOL start_real_item_fury_pulse(ULONGLONG player,ULONGLONG now,RealItemPulseState* st){
    if(!st||st->active)return FALSE;
    ULONGLONG ar=0;if(!resolve_action_request(player,&ar)){log_line("[FURY-ITEM] Could not resolve player's action-request module.");return FALSE;}
    int goods=0,trigger=0;get_companion_ids(&goods,&trigger);
    DWORD raw=((DWORD)goods&0x0FFFFFFFu)|0x40000000u; // ItemCategory::Goods = 4 in high nibble.
    st->ar=ar;st->queued_raw=raw;st->started_at=now;st->active=TRUE;
    st->saved_queued=*(volatile DWORD*)(player+CHR_TAE_QUEUED_USE_ITEM);
    st->saved_action=*(volatile ULONGLONG*)(ar+AR_ACTION_REQUESTS);
    st->saved_prev=*(volatile ULONGLONG*)(ar+AR_PREVIOUS_REQUESTS);
    st->saved_new=*(volatile ULONGLONG*)(ar+AR_NEW_PRESSES);
    st->saved_queued_inputs=*(volatile ULONGLONG*)(ar+AR_QUEUED_INPUTS);
    st->saved_possible=*(volatile ULONGLONG*)(ar+AR_POSSIBLE_INPUTS);
    log_line("[FURY-ITEM] Starting real USE_ITEM pulse with the active Spirit Ash goods ID. This does NOT write a second SummonBuddyManager request.");
    return TRUE;
}

static void drive_real_item_fury_pulse(ULONGLONG player,ULONGLONG now,RealItemPulseState* st){
    if(!st||!st->active)return;
    ULONGLONG ar=0;if(!resolve_action_request(player,&ar)||ar!=st->ar){
        st->active=FALSE;log_line("[FURY-ITEM] Action-request module changed; pulse aborted.");return;
    }
    if(now-st->started_at<g_cfg.real_item_pulse_ms){
