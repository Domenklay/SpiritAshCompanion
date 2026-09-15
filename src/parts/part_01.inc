static const ULONGLONG SBM_PREV_WARN              = 0xB8ull;
static const ULONGLONG SBM_REQUESTED_GOODS        = 0xE0ull;
static const ULONGLONG SBM_ACTIVE_GOODS           = 0xE4ull;
// Elden Ring 1.17.1 fromsoftware-rs: OwnedPtr<SummonBuddyWarpManager> at +0xE8.
static const ULONGLONG SBM_GROUPS                 = 0x70ull;
static const ULONGLONG SBM_WARP_MANAGER           = 0xE8ull;
static const ULONGLONG SBM_SIZE                   = 0x110ull;

// SummonBuddyWarpManager (size 0x38): DLMap is 0x18, followed by native warp thresholds.
// We only tune the two unambiguous engine thresholds: blocked line-of-sight time and distance to player.
static const ULONGLONG BWM_TRIGGER_RAY_BLOCK      = 0x18ull;
static const ULONGLONG BWM_TRIGGER_DIST_PLAYER    = 0x1Cull;
static const ULONGLONG BWM_TRIGGER_PATH_TIME      = 0x20ull;
static const ULONGLONG BWM_TRIGGER_PATH_RANGE     = 0x24ull;
static const float WARP_ASSIST_RAY_BLOCK_SEC      = 0.75f;
static const float WARP_ASSIST_DISTANCE           = 15.0f;
static const float WARP_ASSIST_PATH_TIME_SEC      = 0.75f;
static const float WARP_ASSIST_PATH_RANGE         = 3.0f;

// MSVC DLMap/RbTree + DLList layout used by SummonBuddyManager::groups.
// The current fromsoftware-rs bindings document groups as DLMap<i32, DLList<SummonBuddyGroup>>.
// DLMap: allocator +0x0, sentinel/head +0x8, size +0x10.
// RbNode value begins at +0x20. Pair.first is i32; Pair.second (DLList) is 8-byte aligned at +0x28.
// DLList: allocator +0x0, sentinel/head +0x8, length +0x10. List node: next +0x0, prev +0x8, value +0x10.
// SummonBuddyGroup::warp_requested is documented at +0x20 within the group value.
static const ULONGLONG TREE_HEAD                  = 0x08ull;
static const ULONGLONG TREE_SIZE                  = 0x10ull;
static const ULONGLONG RBN_LEFT                   = 0x00ull;
static const ULONGLONG RBN_RIGHT                  = 0x10ull;
static const ULONGLONG RBN_IS_NIL                 = 0x19ull;
static const ULONGLONG RBN_GROUP_LIST             = 0x28ull;
static const ULONGLONG LIST_HEAD                  = 0x08ull;
static const ULONGLONG LIST_LENGTH                = 0x10ull;
static const ULONGLONG LIST_NODE_NEXT             = 0x00ull;
static const ULONGLONG LIST_NODE_VALUE            = 0x10ull;
static const ULONGLONG GROUP_CHR_INS              = 0x00ull;
static const ULONGLONG GROUP_WARP_REQUESTED       = 0x20ull;
static const ULONGLONG GROUP_DISAPPEAR_REQUESTED  = 0x21ull;

struct Config {
    DWORD start_delay_ms;
    DWORD poll_ms;
    DWORD respawn_delay_ms;
    DWORD request_cooldown_ms;
    DWORD fury_delay_ms;
    DWORD fury_refresh_ms;
    int enable_summon_anywhere;
    int force_buddy_activation;
    int prevent_manager_disappear;
    int force_spirit_fury;
    int enable_real_item_fury;
    DWORD real_item_fury_delay_ms;
    DWORD real_item_pulse_ms;
    int preset;
    int enable_debug_npc_probe;
    DWORD debug_npc_delay_ms;
    int debug_chr_id;
    int debug_chara_init_param_id;
    int debug_npc_param_id;
    int debug_npc_think_param_id;
    int debug_event_entity_id;
    int debug_talk_id;
    int debug_offset_x_cm;
};
static Config g_cfg={8000,100,2500,6000,1200,2000,1,1,1,1,0,3500,700,0,1,1800,0,23640,100000050,100000050,0,0,250};

static DWORD ascii_len(const char* s){DWORD n=0;while(s&&s[n])++n;return n;}
static void bytes_copy(void* dst,const void* src,SIZE_T n){volatile BYTE* d=(volatile BYTE*)dst;const volatile BYTE* s=(const volatile BYTE*)src;for(SIZE_T i=0;i<n;++i)d[i]=s[i];}
static void build_sibling_path(wchar_t* out,SIZE_T cap,const wchar_t* filename){
    if(!out||cap<2)return; wchar_t tmp[1024];for(SIZE_T i=0;i<1024;++i)tmp[i]=0;
    DWORD n=GetModuleFileNameW(g_self,tmp,1023);SIZE_T cut=(SIZE_T)n;
    while(cut>0){wchar_t c=tmp[cut-1];if(c==L'\\'||c==L'/')break;--cut;}
    SIZE_T i=0;while(i<cut&&i+1<cap){out[i]=tmp[i];++i;}SIZE_T j=0;while(filename&&filename[j]&&i+1<cap)out[i++]=filename[j++];out[i]=0;
}
static void log_line(const char* text){
    if(!g_log_path[0]||!text)return;HANDLE h=CreateFileW(g_log_path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    if(!h||h==INVALID_HANDLE_VALUE)return;DWORD wrote=0,len=ascii_len(text);if(len)WriteFile(h,text,len,&wrote,0);const char crlf[2]={'\r','\n'};WriteFile(h,crlf,2,&wrote,0);CloseHandle(h);
}
static void log_i32_line(const char* prefix,int value){
    char buf[192];DWORD n=0;
    if(prefix){while(prefix[n]&&n+2<sizeof(buf)){buf[n]=prefix[n];++n;}}
    long long v=value; if(v<0){buf[n++]='-';v=-v;}
    char tmp[24];DWORD t=0; do{tmp[t++]=(char)('0'+(v%10));v/=10;}while(v&&t<sizeof(tmp));
    while(t&&n+1<sizeof(buf))buf[n++]=tmp[--t];buf[n]=0;log_line(buf);
}

static void popup(const wchar_t* body,const wchar_t* title,BOOL warn){MessageBoxW(0,body,title,MB_OK|(warn?MB_ICONWARNING:MB_ICONINFORMATION));}
static BOOL readable_range(const void* ptr,SIZE_T size){
    if(!ptr||!size)return FALSE;const BYTE* cur=(const BYTE*)ptr;const BYTE* end=cur+size;if(end<cur)return FALSE;
    while(cur<end){MEMORY_BASIC_INFORMATION_X64 mbi;if(!VirtualQuery(cur,&mbi,sizeof(mbi)))return FALSE;if(mbi.State!=MEM_COMMIT)return FALSE;if((mbi.Protect&PAGE_GUARD)||(mbi.Protect&PAGE_NOACCESS))return FALSE;const BYTE* re=(const BYTE*)mbi.BaseAddress+mbi.RegionSize;if(re<=cur)return FALSE;cur=re;}return TRUE;
}
// v3.4: normal healthy-state polling must not call VirtualQuery at all.
// SEH turns a stale pointer during a level transition into a cheap cache miss instead of a crash.
static BOOL seh_read_u64(const volatile ULONGLONG* p,ULONGLONG* out){
    if(!p||!out)return FALSE;
    __try{*out=*p;return TRUE;}__except(1){*out=0;return FALSE;}
}
static BOOL seh_read_u8(const volatile BYTE* p,BYTE* out){
    if(!p||!out)return FALSE;
    __try{*out=*p;return TRUE;}__except(1){*out=0;return FALSE;}
}
static BOOL seh_read_u32(const volatile DWORD* p,DWORD* out){
    if(!p||!out)return FALSE;
    __try{*out=*p;return TRUE;}__except(1){*out=0;return FALSE;}
}
static BOOL seh_read_i32(const volatile int* p,int* out){
    if(!p||!out)return FALSE;
    __try{*out=*p;return TRUE;}__except(1){*out=0;return FALSE;}
}
static BOOL seh_read_f32(const volatile float* p,float* out){
    if(!p||!out)return FALSE;
    __try{*out=*p;return TRUE;}__except(1){*out=0.0f;return FALSE;}
}
static BOOL seh_write_u8(volatile BYTE* p,BYTE value){
    if(!p)return FALSE;
    __try{*p=value;return TRUE;}__except(1){return FALSE;}
}
static BOOL seh_write_f32(volatile float* p,float value){
    if(!p)return FALSE;
    __try{*p=value;return TRUE;}__except(1){return FALSE;}
}
static BOOL safe_cstr_eq(const char* p,const char* target){if(!p||!target)return FALSE;for(DWORD i=0;i<96;++i){if(!readable_range(p+i,1))return FALSE;char a=p[i],b=target[i];if(a!=b)return FALSE;if(!b)return TRUE;}return FALSE;}

struct ModuleInfo{BYTE* base;DWORD size_of_image;WORD number_of_sections;BYTE* section_table;};
struct SectionView{BYTE* ptr;DWORD size;DWORD rva;};
static BOOL get_main_module_info(ModuleInfo* out){
    if(!out)return FALSE;BYTE* base=(BYTE*)GetModuleHandleW(0);if(!base||!readable_range(base,0x1000)||*(WORD*)base!=0x5A4D)return FALSE;
    DWORD e=*(DWORD*)(base+0x3C);BYTE* nt=base+e;if(!readable_range(nt,0x200)||*(DWORD*)nt!=0x00004550u)return FALSE;
    WORD sections=*(WORD*)(nt+6),opt_size=*(WORD*)(nt+20);BYTE* opt=nt+24;if(*(WORD*)opt!=0x20B)return FALSE;DWORD image=*(DWORD*)(opt+56);BYTE* sec=opt+opt_size;
    if(!sections||sections>96||image<0x100000)return FALSE;out->base=base;out->size_of_image=image;out->number_of_sections=sections;out->section_table=sec;return TRUE;
}
static BOOL secname_eq(BYTE* sh,const char* name){for(int i=0;i<8;++i){char a=(char)sh[i],b=name[i];if(a!=b)return FALSE;if(!b)return TRUE;}return name[8]==0;}
static BOOL get_section(const ModuleInfo* mi,const char* name,SectionView* out){
    if(!mi||!name||!out)return FALSE;BYTE* sh=mi->section_table;for(WORD s=0;s<mi->number_of_sections;++s,sh+=40){if(!secname_eq(sh,name))continue;DWORD vs=*(DWORD*)(sh+8),va=*(DWORD*)(sh+12),rs=*(DWORD*)(sh+16),sz=vs>rs?vs:rs;if(va>=mi->size_of_image)return FALSE;if(va+sz>mi->size_of_image)sz=mi->size_of_image-va;BYTE* ptr=mi->base+va;if(!readable_range(ptr,sz))return FALSE;out->ptr=ptr;out->size=sz;out->rva=va;return TRUE;}return FALSE;
}
static int read_i32(const BYTE* p){return *(const volatile int*)p;}
static BYTE* rip_target(BYTE* disp32){return disp32+4+(LONGLONG)read_i32(disp32);}
static BOOL ptr_in_section(BYTE* p,const SectionView* s){return p>=s->ptr&&p<s->ptr+s->size;}

enum InstrKind{IK_NONE=0,IK_LEA_RCX=1,IK_LEA_R8=2,IK_LEA_R9=3,IK_MOV_R9=4,IK_MOV_EDX=5};
struct Instr{BYTE kind;DWORD pos;};
static DWORD instr_len(BYTE kind){if(kind==IK_MOV_EDX)return 5;if(kind==IK_MOV_R9)return 3;if(kind==IK_LEA_RCX||kind==IK_LEA_R8||kind==IK_LEA_R9)return 7;return 0;}
static BYTE classify3(const BYTE* p){if(p[0]==0x48&&p[1]==0x8D&&p[2]==0x0D)return IK_LEA_RCX;if(p[0]==0x4C&&p[1]==0x8D&&p[2]==0x05)return IK_LEA_R8;if(p[0]==0x4C&&p[1]==0x8D&&p[2]==0x0D)return IK_LEA_R9;if(p[0]==0x4C&&p[1]==0x8B&&p[2]==0xC8)return IK_MOV_R9;return IK_NONE;}
static BOOL cond_jump_before(const BYTE* text,DWORD,DWORD pos,DWORD* out_pos){if(pos>=2&&text[pos-2]==0x75){*out_pos=pos-2;return TRUE;}if(pos>=6&&text[pos-6]==0x0F&&text[pos-5]==0x85){*out_pos=pos-6;return TRUE;}return FALSE;}
static BYTE* find_singleton_addr_disp(BYTE* text,DWORD text_size,DWORD candidate_pos){
    DWORD jump=0;if(!cond_jump_before(text,text_size,candidate_pos,&jump)||jump<3)return 0;DWORD tpos=jump-3;BYTE* test=text+tpos;BYTE test_rex=test[0],test_modrm=test[2],test_rexb=test_rex&1;BYTE test_mod=test_modrm&0xC0,test_reg1=test_modrm&7,test_reg2=(test_modrm>>3)&7;const BYTE REX_MASK=(BYTE)~(0x4F^0x48);if((test_rex&REX_MASK)!=0x48||test_mod!=0xC0||test_reg1!=test_reg2)return 0;
    for(DWORD pad=0;pad<=3;++pad){if(tpos<7+pad)continue;DWORD mpos=tpos-(7+pad);BYTE* mov=text+mpos;BYTE mov_rex=mov[0],mov_modrm=mov[2],mov_rexw=(mov_rex>>2)&1;BYTE mov_mod=mov_modrm&0xC0,mov_mem=mov_modrm&7,mov_reg=(mov_modrm>>3)&7;if((mov_rex&REX_MASK)==0x48&&mov_mod==0&&mov_mem==5&&mov_rexw==test_rexb&&mov_reg==test_reg1)return mov+3;}return 0;
}
static BOOL build_candidate(BYTE* text,DWORD size,DWORD ba_pos,Instr out[4],DWORD* out_mask){
    Instr ins[4];int count=1;ins[0].kind=IK_MOV_EDX;ins[0].pos=ba_pos;while(count<4){DWORD next=ins[count-1].pos+instr_len(ins[count-1].kind);if(next+3>size)return FALSE;BYTE kind=classify3(text+next);if(kind==IK_LEA_RCX||kind==IK_LEA_R8||kind==IK_LEA_R9||kind==IK_MOV_R9){ins[count].kind=kind;ins[count].pos=next;++count;continue;}if(text[next]==0xE8)break;return FALSE;}
    while(count<4){DWORD first=ins[0].pos;if(first<7)return FALSE;DWORD prev=first-7;BYTE kind=classify3(text+prev);DWORD ipos=prev;if(!(kind==IK_LEA_RCX||kind==IK_LEA_R8||kind==IK_LEA_R9)){if(text[prev+4]==0x4C&&text[prev+5]==0x8B&&text[prev+6]==0xC8){kind=IK_MOV_R9;ipos=prev+4;}else return FALSE;}for(int j=count;j>0;--j)ins[j]=ins[j-1];ins[0].kind=kind;ins[0].pos=ipos;++count;}
    DWORD mask=0;for(int i=0;i<4;++i){out[i]=ins[i];if(ins[i].kind==IK_LEA_RCX)mask|=1;else if(ins[i].kind==IK_LEA_R8)mask|=2;else if(ins[i].kind==IK_LEA_R9)mask|=4;else if(ins[i].kind==IK_MOV_R9)mask|=8;}*out_mask=mask;return TRUE;
}
typedef const char* (*GetReflectionNameFn)(const void* reflection);
static BYTE* scan_singleton_global_modern(const char* wanted){
    ModuleInfo mi;SectionView text,data;if(!get_main_module_info(&mi)||!get_section(&mi,".text",&text)||!get_section(&mi,".data",&data))return 0;
    for(DWORD i=0;i<text.size;++i){if(text.ptr[i]!=0xBA)continue;Instr ins[4];DWORD mask=0;if(!build_candidate(text.ptr,text.size,i,ins,&mask))continue;
        if(mask==7){DWORD cap_name=0;for(int k=0;k<4;++k)if(ins[k].kind==IK_LEA_R9){cap_name=ins[k].pos+3;break;}if(!cap_name||cap_name+4>text.size)continue;BYTE* addr_disp=find_singleton_addr_disp(text.ptr,text.size,ins[0].pos);if(!addr_disp)continue;BYTE* global=rip_target(addr_disp);if(!ptr_in_section(global,&data))continue;BYTE* name=rip_target(text.ptr+cap_name);if(!readable_range(name,1))continue;if(safe_cstr_eq((const char*)name,wanted))return global;}
        else if(mask==11){DWORD first=ins[0].pos;for(DWORD callpad=0;callpad<=1;++callpad){if(first<5+callpad)continue;DWORD callpos=first-(5+callpad);if(text.ptr[callpos]!=0xE8||callpos<7)continue;DWORD leapos=callpos-7;if(!(text.ptr[leapos]==0x48&&text.ptr[leapos+1]==0x8D&&text.ptr[leapos+2]==0x0D))continue;BYTE* refl_disp=text.ptr+leapos+3;BYTE* addr_disp=find_singleton_addr_disp(text.ptr,text.size,leapos);if(!addr_disp)continue;BYTE* global=rip_target(addr_disp);if(!ptr_in_section(global,&data))continue;BYTE* reflection=rip_target(refl_disp);BYTE* fn=rip_target(text.ptr+callpos+1);if(!ptr_in_section(fn,&text)||!readable_range(reflection,8))continue;GetReflectionNameFn get_name=(GetReflectionNameFn)fn;const char* name=get_name((const void*)reflection);if(name&&safe_cstr_eq(name,wanted))return global;}}
    }return 0;
}
static BYTE* scan_worldchrman_global_modern(){
    log_line("[SCAN] Dantelion2 WorldChrMan scan started.");
    BYTE* g=scan_singleton_global_modern("WorldChrMan");
    log_line(g?"[SCAN] WorldChrMan found (reflection/derived singleton).":"[SCAN] FAIL WorldChrMan not found.");
    return g;
}
static BYTE* scan_soloparam_global_modern(){
    log_line("[PARAM-REPO] Probing engine SoloParamRepository singleton (no PARAM memory scan).");
    BYTE* g=scan_singleton_global_modern("SoloParamRepository");
    log_line(g?"[PARAM-REPO] SoloParamRepository singleton global found.":"[PARAM-REPO] SoloParamRepository singleton was not resolved by reflection probe; gameplay path continues normally.");
    return g;
}

static BOOL pattern_match(const BYTE* p,const BYTE* bytes,const char* mask,SIZE_T n){for(SIZE_T i=0;i<n;++i)if(mask[i]=='x'&&p[i]!=bytes[i])return FALSE;return TRUE;}
static BYTE* scan_text(const BYTE* bytes,const char* mask,SIZE_T n){ModuleInfo mi;SectionView text;if(!get_main_module_info(&mi)||!get_section(&mi,".text",&text))return 0;for(DWORD i=0;i+n<=text.size;++i)if(pattern_match(text.ptr+i,bytes,mask,n))return text.ptr+i;return 0;}
static LPVOID alloc_near(BYTE* target,SIZE_T size){
    const ULONGLONG GRAN=0x10000ull;ULONGLONG t=(ULONGLONG)target,low=(t>0x70000000ull)?t-0x70000000ull:GRAN,high=t+0x70000000ull;low&=~(GRAN-1);high&=~(GRAN-1);
    for(ULONGLONG d=0;d<=0x70000000ull;d+=GRAN){ULONGLONG tries[2]={(t>=d)?((t-d)&~(GRAN-1)):0,(t+d)&~(GRAN-1)};for(int k=0;k<2;++k){ULONGLONG a=tries[k];if(!a||a<low||a>high)continue;MEMORY_BASIC_INFORMATION_X64 mbi;if(!VirtualQuery((LPCVOID)a,&mbi,sizeof(mbi))||mbi.State!=MEM_FREE)continue;ULONGLONG base=(ULONGLONG)mbi.BaseAddress,end=base+mbi.RegionSize,candidate=a;if(candidate<base)candidate=base;candidate=(candidate+GRAN-1)&~(GRAN-1);if(candidate+size>end)continue;LPVOID mem=VirtualAlloc((LPVOID)candidate,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);if(mem)return mem;}}return 0;
}
static BOOL write_rel32_jmp(BYTE* at,SIZE_T overwrite_len,BYTE* destination,BYTE* old_bytes){
    if(!at||!destination||overwrite_len<5||!old_bytes)return FALSE;LONGLONG rel64=(LONGLONG)(destination-(at+5));if(rel64<-2147483648ll||rel64>2147483647ll)return FALSE;DWORD oldp=0;if(!VirtualProtect(at,overwrite_len,PAGE_EXECUTE_READWRITE,&oldp))return FALSE;for(SIZE_T i=0;i<overwrite_len;++i)old_bytes[i]=at[i];at[0]=0xE9;*(int*)(at+1)=(int)rel64;for(SIZE_T i=5;i<overwrite_len;++i)at[i]=0x90;FlushInstructionCache(GetCurrentProcess(),at,overwrite_len);DWORD ignored=0;VirtualProtect(at,overwrite_len,oldp,&ignored);return TRUE;
}
static BOOL install_summon_anywhere_patch1(){
    static const BYTE p[]={0x48,0x8B,0x47,0,0xF3,0x0F,0x10,0x90,0,0,0,0,0x0F,0x2F,0xD0};static const char m[]="xxx?xxxx????xxx";BYTE* scan=scan_text(p,m,sizeof(p));if(!scan)return FALSE;
    BYTE code[]={0x48,0x8B,0x47,0x28,0x81,0x38,0xD0,0x07,0x00,0x00,0x0F,0x85,0x0A,0x00,0x00,0x00,0xF3,0x0F,0x10,0x15,0x0F,0x00,0x00,0x00,0xEB,0x08,0xF3,0x0F,0x10,0x90,0x84,0x00,0x00,0x00,0xE9,0,0,0,0,0x00,0x00,0x7A,0x44};code[3]=scan[3];BYTE* tramp=(BYTE*)alloc_near(scan,sizeof(code));if(!tramp)return FALSE;LONGLONG back=(LONGLONG)((scan+12)-(tramp+34+5));if(back<-2147483648ll||back>2147483647ll)return FALSE;*(int*)&code[35]=(int)back;bytes_copy(tramp,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),tramp,sizeof(code));static BYTE old[12];return write_rel32_jmp(scan,12,tramp,old);
}
static BOOL install_summon_anywhere_patch2(){
    static const BYTE p[]={0x48,0x8B,0x45,0x98,0x48,0x85,0xC0,0x0F,0x84,0,0,0,0,0x8B,0x40,0x20};static const char m[]="xxxxxxxxx????xxx";BYTE* scan=scan_text(p,m,sizeof(p));if(!scan)return FALSE;
    BYTE code[]={0x48,0x8B,0x45,0x98,0x48,0x85,0xC0,0x0F,0x84,0x0D,0x00,0x00,0x00,0xC7,0x40,0x20,0x00,0x00,0x00,0x00,0x66,0xC7,0x40,0x1C,0xFF,0xFF,0x48,0x85,0xC0,0xE9,0,0,0,0};BYTE* tramp=(BYTE*)alloc_near(scan,sizeof(code));if(!tramp)return FALSE;LONGLONG back=(LONGLONG)((scan+7)-(tramp+29+5));if(back<-2147483648ll||back>2147483647ll)return FALSE;*(int*)&code[30]=(int)back;bytes_copy(tramp,code,sizeof(code));FlushInstructionCache(GetCurrentProcess(),tramp,sizeof(code));static BYTE old[7];return write_rel32_jmp(scan,7,tramp,old);
}


// v0.8 manager/runtime-loadout additions.
static const ULONGLONG CHR_MODULES_PHYSICS       = 0x68ull;
static const ULONGLONG PHYSICS_POSITION          = 0x70ull;
static const ULONGLONG PHYSICS_LAST_UPDATE_POS   = 0x80ull;
static const ULONGLONG PHYSICS_PROXY_POS_REQUEST = 0x91ull;
static const float DIRECT_CATCHUP_DISTANCE        = 18.0f;
static const ULONGLONG DIRECT_CATCHUP_COOLDOWN_MS = 6000ull;
