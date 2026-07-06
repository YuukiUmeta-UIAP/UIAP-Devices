/*
 * SDmin.h — Minimal FAT32 SD driver with LFN + subdirectory support
 *
 * Filenames: up to 26 chars (2 LFN entries × 13)
 * Supports: FAT32, 512-byte sectors, subdirectories
 */
#pragma once
#include <Arduino.h>

// ── Volume / file state ──────────────────────────────────────────────────────
static uint8_t  sm_buf[512];
static uint8_t  sm_cs, sm_sdhc;
static uint32_t sm_fat_lba, sm_root_clus, sm_data_lba;
static uint8_t  sm_spc;

static uint8_t  sm_f_open;
static uint32_t sm_f_start, sm_f_prev, sm_f_cur;
static uint8_t  sm_f_sec;
static uint16_t sm_f_off;
static uint32_t sm_f_pos, sm_f_size, sm_f_dir_lba;
static uint8_t  sm_f_dir_ent;

// ── Shared LFN scan state ────────────────────────────────────────────────────
static char     _sm_lfn[27];
static uint32_t _sm_lfn_lba[2];
static uint8_t  _sm_lfn_idx[2], _sm_lfn_cnt;
static char     _sm_comp[27]; // path component buffer

static void _sm_lfn_rst(void) {
  memset(_sm_lfn, 0, sizeof(_sm_lfn)); _sm_lfn_cnt = 0;
}

typedef struct { uint32_t cl; uint16_t sec; uint8_t ent; } SmListCtx;

// ── SPI / SD low-level ───────────────────────────────────────────────────────
// CH32V003: SPIClass.beginTransaction()はクロック設定が機能しないため直接レジスタ使用
static uint8_t _sm_xfer(uint8_t b) {
  while(!(SPI1->STATR&(1u<<1)));
  SPI1->DATAR=b;
  while(!(SPI1->STATR&(1u<<0)));
  return (uint8_t)SPI1->DATAR;
}
static uint8_t _sm_cmd(uint8_t cmd, uint32_t arg) {
  _sm_xfer(0x40|cmd);
  _sm_xfer(arg>>24); _sm_xfer(arg>>16);
  _sm_xfer(arg>>8);  _sm_xfer(arg);
  _sm_xfer(cmd==0?0x95:cmd==8?0x87:0xFF);
  for(uint8_t n=0;n<10;n++){uint8_t r=_sm_xfer(0xFF);if(!(r&0x80))return r;}
  return 0xFF;
}
static bool _sm_rblk(uint32_t lba) {
  uint32_t a=sm_sdhc?lba:lba<<9;
  digitalWrite(sm_cs,LOW);
  if(_sm_cmd(17,a)){digitalWrite(sm_cs,HIGH);return false;}
  for(uint32_t t=0;t<200000UL;t++) if(_sm_xfer(0xFF)==0xFE) goto ok;
  digitalWrite(sm_cs,HIGH); return false;
ok:
  for(uint16_t i=0;i<512;i++) sm_buf[i]=_sm_xfer(0xFF);
  _sm_xfer(0xFF); _sm_xfer(0xFF);
  digitalWrite(sm_cs,HIGH); return true;
}
static bool _sm_wblk(uint32_t lba) {
  uint32_t a=sm_sdhc?lba:lba<<9;
  digitalWrite(sm_cs,LOW);
  if(_sm_cmd(24,a)){digitalWrite(sm_cs,HIGH);return false;}
  _sm_xfer(0xFF); _sm_xfer(0xFE);
  for(uint16_t i=0;i<512;i++) _sm_xfer(sm_buf[i]);
  _sm_xfer(0xFF); _sm_xfer(0xFF);
  // Data response token: SD spec allows up to 8 bytes after CRC before token appears
  uint8_t dr=0xFF;
  for(uint8_t n=0;n<8;n++){dr=_sm_xfer(0xFF);if(dr!=0xFF)break;}
  dr&=0x1F;
  // SD spec: single block write time up to 250ms; at 6MHz each xfer ~1.33us → 200000 covers ~266ms
  for(uint32_t t=0;t<200000UL;t++) if(_sm_xfer(0xFF)) break;
  digitalWrite(sm_cs,HIGH); return dr==0x05;
}

// ── SD init ──────────────────────────────────────────────────────────────────
bool sm_init(uint8_t cs) {
  sm_cs=cs; sm_sdhc=0; sm_f_open=0;
  pinMode(cs,OUTPUT); digitalWrite(cs,HIGH);
  // SPI1直接初期化: GPIOC+SPI1クロック、PC5=SCK/PC6=MOSI/PC7=MISO、BR=/256≒187kHz
  RCC->APB2PCENR |= (1u<<4)|(1u<<12);
  GPIOC->CFGLR = (GPIOC->CFGLR & ~((0xFu<<20)|(0xFu<<24)|(0xFu<<28)))
               | (0xBu<<20)|(0xBu<<24)|(0x8u<<28);
  GPIOC->BSHR = (1u<<7);
  SPI1->CTLR2 = 0;
  SPI1->CTLR1 = (7u<<3)|(1u<<2)|(1u<<9)|(1u<<8)|(1u<<6);
  for(uint8_t i=0;i<10;i++) _sm_xfer(0xFF);
  digitalWrite(cs,LOW); uint8_t r=_sm_cmd(0,0); digitalWrite(cs,HIGH);
  if(r!=1) return false;
  uint8_t v2=0;
  digitalWrite(cs,LOW);
  if(_sm_cmd(8,0x1AAUL)==1){
    _sm_xfer(0xFF);_sm_xfer(0xFF);_sm_xfer(0xFF);
    v2=(_sm_xfer(0xFF)==0xAA)?1:0;
  }
  digitalWrite(cs,HIGH);
  for(uint16_t i=0;i<2000;i++){
    digitalWrite(cs,LOW);_sm_cmd(55,0);digitalWrite(cs,HIGH);
    digitalWrite(cs,LOW);r=_sm_cmd(41,v2?0x40000000UL:0);digitalWrite(cs,HIGH);
    if(!r) break; delay(1);
  }
  if(r) return false;
  if(v2){
    digitalWrite(cs,LOW);_sm_cmd(58,0);
    sm_sdhc=(_sm_xfer(0xFF)&0x40)?1:0;
    _sm_xfer(0xFF);_sm_xfer(0xFF);_sm_xfer(0xFF);
    digitalWrite(cs,HIGH);
  }
  if(!sm_sdhc){
    digitalWrite(cs,LOW);r=_sm_cmd(16,512);digitalWrite(cs,HIGH);
    if(r) return false;
  }
  // 高速転送に切り替え: BR=/8≒6MHz
  SPI1->CTLR1 = (SPI1->CTLR1 & ~(7u<<3))|(2u<<3);
  return true;
}

// ── FAT32 mount ──────────────────────────────────────────────────────────────
bool sm_mount(void) {
  if(!_sm_rblk(0)) return false;
  uint32_t base=0;
  if(sm_buf[510]==0x55&&sm_buf[511]==0xAA&&sm_buf[0]!=0xEB&&sm_buf[0]!=0xE9){
    uint8_t*p=sm_buf+446;
    base=(uint32_t)p[8]|((uint32_t)p[9]<<8)|((uint32_t)p[10]<<16)|((uint32_t)p[11]<<24);
    if(!_sm_rblk(base)) return false;
  }
  if(sm_buf[11]!=0||sm_buf[12]!=2) return false;
  sm_spc=(uint8_t)sm_buf[13];
  uint16_t rsv=(uint16_t)sm_buf[14]|((uint16_t)sm_buf[15]<<8);
  uint8_t  nft=sm_buf[16];
  if(!sm_spc) return false;
  uint32_t fps=(uint32_t)sm_buf[36]|((uint32_t)sm_buf[37]<<8)|((uint32_t)sm_buf[38]<<16)|((uint32_t)sm_buf[39]<<24);
  if(!fps) return false;
  sm_root_clus=(uint32_t)sm_buf[44]|((uint32_t)sm_buf[45]<<8)|((uint32_t)sm_buf[46]<<16)|((uint32_t)sm_buf[47]<<24);
  sm_fat_lba=base+rsv;
  sm_data_lba=sm_fat_lba+(uint32_t)nft*fps;
  return true;
}

// ── FAT helpers ──────────────────────────────────────────────────────────────
static bool _sm_eoc(uint32_t cl){ return (cl&0x0FFFFFFFUL)>=0x0FFFFFF8UL; }
static uint32_t _sm_fget(uint32_t cl){
  uint32_t off=cl<<2;
  if(!_sm_rblk(sm_fat_lba+off/512)) return 0;
  uint16_t i=off&511;
  return ((uint32_t)sm_buf[i]|((uint32_t)sm_buf[i+1]<<8)|((uint32_t)sm_buf[i+2]<<16)|((uint32_t)sm_buf[i+3]<<24))&0x0FFFFFFFUL;
}
static bool _sm_fset(uint32_t cl,uint32_t val){
  uint32_t off=cl<<2;
  uint32_t lba=sm_fat_lba+off/512;
  if(!_sm_rblk(lba)) return false;
  uint16_t i=off&511;
  sm_buf[i]=val;sm_buf[i+1]=val>>8;sm_buf[i+2]=val>>16;sm_buf[i+3]=(sm_buf[i+3]&0xF0)|((val>>24)&0x0F);
  return _sm_wblk(lba);
}
static uint32_t _sm_falloc(void){
  uint32_t cur=0xFFFFFFFFUL;
  for(uint32_t cl=2;cl<0x0FFFFFF7UL;cl++){
    uint32_t off=cl<<2;
    uint32_t lba=sm_fat_lba+off/512;
    if(lba!=cur){if(!_sm_rblk(lba))return 0;cur=lba;}
    uint16_t i=off&511;
    if(!sm_buf[i]&&!sm_buf[i+1]&&!sm_buf[i+2]&&!(sm_buf[i+3]&0x0F)) return cl;
  }
  return 0;
}

// ── Directory sector iteration ───────────────────────────────────────────────
// cl=0 in ctx means root cluster
static uint32_t _sm_dir_lba(const SmListCtx*ctx){
  uint32_t cl=ctx->cl?ctx->cl:sm_root_clus;
  if(_sm_eoc(cl)) return 0;
  return sm_data_lba+(uint32_t)(cl-2)*sm_spc+ctx->sec;
}
static uint32_t _sm_dir_next(SmListCtx*ctx){
  ctx->ent=0;ctx->sec++;
  if(ctx->sec<sm_spc) return _sm_dir_lba(ctx);
  ctx->sec=0;
  uint32_t cur=ctx->cl?ctx->cl:sm_root_clus;
  ctx->cl=_sm_fget(cur);
  return _sm_dir_lba(ctx);
}

// ── LFN helpers ──────────────────────────────────────────────────────────────
static uint8_t _lfo(uint8_t i){
  if(i<5)  return 1+(i<<1);
  if(i<11) return 14+((i-5)<<1);
  return          28+((i-11)<<1);
}
static uint8_t _sm_lfn_cksum(const char*s){
  uint8_t r=0;for(uint8_t i=0;i<11;i++) r=((r&1)<<7)+(r>>1)+(uint8_t)s[i];return r;
}
static void _sm_lfn_acc(const uint8_t*d,uint32_t lba,uint8_t idx){
  uint8_t seq=d[0]&0x1F;
  if(!seq||seq>2) return;
  if(_sm_lfn_cnt<2){_sm_lfn_lba[_sm_lfn_cnt]=lba;_sm_lfn_idx[_sm_lfn_cnt]=idx;_sm_lfn_cnt++;}
  uint8_t base=(seq-1)*13;
  for(uint8_t i=0;i<13;i++){
    uint8_t pos=base+i; if(pos>25) break;
    uint8_t lo=d[_lfo(i)],hi=d[_lfo(i)+1];
    if(lo==0xFF&&hi==0xFF) break;
    if(!lo&&!hi){_sm_lfn[pos]=0;return;}
    _sm_lfn[pos]=hi?'?':lo;
  }
}
static void _sm_lfn_bld(uint8_t*dst,uint8_t sb,const char*n,uint8_t nl,uint8_t ck){
  memset(dst,0,32);
  dst[0]=sb;dst[11]=0x0F;dst[13]=ck;
  uint8_t seq=sb&0x1F,base=(seq-1)*13;bool pad=false;
  for(uint8_t i=0;i<13;i++){
    uint16_t ch;uint8_t pos=base+i;
    if(pad) ch=0xFFFF;
    else if(pos<nl) ch=(uint8_t)n[pos];
    else{ch=0;pad=true;}
    dst[_lfo(i)]=ch;dst[_lfo(i)+1]=ch>>8;
  }
}

// ── Filename helpers ─────────────────────────────────────────────────────────
static char _uc(char c){return(c>='a'&&c<='z')?c-32:c;}
static void _sm_gen_sfn(const char*name,char*sfn){
  memset(sfn,' ',11);
  uint8_t j=0;
  for(uint8_t i=0;name[i]&&name[i]!='.'&&j<6;i++){
    char c=_uc(name[i]);sfn[j++]=(c==' ')?'_':c;
  }
  sfn[j]='~';sfn[j+1]='1';
  const char*dot=strrchr(name,'.');
  if(dot){j=8;for(uint8_t i=1;dot[i]&&j<11;i++,j++) sfn[j]=_uc(dot[i]);}
}
static bool _sm_ci_eq(const char*a,const char*b){
  while(*a&&*b){if(_uc(*a)!=_uc(*b))return false;a++;b++;}
  return *a==*b;
}
static uint32_t _sm_get_clus(const uint8_t*d){
  return (uint32_t)d[26]|((uint32_t)d[27]<<8)|((uint32_t)d[20]<<16)|((uint32_t)d[21]<<24);
}
static void _sm_set_clus(uint8_t*d,uint32_t cl){
  d[26]=cl;d[27]=cl>>8;d[20]=cl>>16;d[21]=cl>>24;
}

// ── Directory search (any cluster, file or dir) ───────────────────────────────
typedef struct { uint32_t cluster,size,dir_lba; uint8_t dir_ent; } _SmEntry;

// want_dir: true=find directory entry, false=find file entry
static bool _sm_find_cl(uint32_t dir_cl,const char*name,_SmEntry*e,bool want_dir){
  _sm_lfn_rst();
  SmListCtx ctx; ctx.cl=dir_cl;ctx.sec=0;ctx.ent=0;
  uint32_t lba=_sm_dir_lba(&ctx);
  while(lba){
    if(!_sm_rblk(lba)) return false;
    while(ctx.ent<16){
      uint8_t*d=sm_buf+ctx.ent*32;
      uint8_t first=d[0];
      if(!first) return false;
      if(first==0xE5){ctx.ent++;_sm_lfn_rst();continue;}
      if(d[11]==0x0F){_sm_lfn_acc(d,lba,ctx.ent);ctx.ent++;continue;}
      if(d[11]&0x08){ctx.ent++;_sm_lfn_rst();continue;} // skip volume label
      bool is_dir=(d[11]&0x10)!=0;
      if(is_dir!=want_dir){ctx.ent++;_sm_lfn_rst();continue;}
      bool match;
      if(_sm_lfn[0]){
        match=_sm_ci_eq(_sm_lfn,name);
      } else {
        char sfn[13];uint8_t n=0;
        for(uint8_t k=0;k<8&&d[k]!=' ';k++) sfn[n++]=d[k];
        sfn[n++]='.';uint8_t ext=0;
        for(uint8_t k=8;k<11;k++) if(d[k]!=' '){sfn[n++]=d[k];ext++;}
        if(!ext) n--;
        sfn[n]=0;
        match=_sm_ci_eq(sfn,name);
      }
      if(match&&e){
        e->cluster=_sm_get_clus(d);
        e->size=(uint32_t)d[28]|((uint32_t)d[29]<<8)|((uint32_t)d[30]<<16)|((uint32_t)d[31]<<24);
        e->dir_lba=lba;e->dir_ent=ctx.ent;
      }
      if(match) return true;
      ctx.ent++;_sm_lfn_rst();
    }
    lba=_sm_dir_next(&ctx);
  }
  return false;
}

// Find 'cnt' consecutive free dir slots in dir_cl
static bool _sm_dir_free_cl(uint32_t dir_cl,uint8_t cnt,SmListCtx*out){
  uint8_t run=0; SmListCtx rs;
  SmListCtx c; c.cl=dir_cl;c.sec=0;c.ent=0;
  uint32_t lba=_sm_dir_lba(&c);
  while(lba){
    if(!_sm_rblk(lba)) return false;
    while(c.ent<16){
      uint8_t fb=sm_buf[c.ent*32];
      if(!fb||fb==0xE5){if(!run)rs=c;if(++run>=cnt){*out=rs;return true;}if(!fb){*out=rs;return true;}}
      else run=0;
      c.ent++;
    }
    lba=_sm_dir_next(&c);
  }
  return false;
}

// ── Path resolver ─────────────────────────────────────────────────────────────
// Splits "DIR/SUBDIR/FILE" → dir_cl=cluster of SUBDIR, base="FILE"
// Returns false if any intermediate directory is not found
static bool _sm_resolve(const char*path,uint32_t*dir_cl,const char**base){
  *dir_cl=0; // 0 = root
  const char*p=path;
  if(*p=='/') p++;
  for(;;){
    const char*sl=strchr(p,'/');
    if(!sl){*base=p;return true;}
    uint8_t len=(uint8_t)(sl-p);
    if(!len||len>=27) return false;
    memcpy(_sm_comp,p,len);_sm_comp[len]=0;
    _SmEntry e;
    if(!_sm_find_cl(*dir_cl,_sm_comp,&e,true)) return false;
    *dir_cl=e.cluster;
    p=sl+1;
  }
}

// ── File open for read ────────────────────────────────────────────────────────
bool sm_open_r(const char*path){
  if(sm_f_open) return false;
  uint32_t dcl;const char*fname;
  if(!_sm_resolve(path,&dcl,&fname)) return false;
  _SmEntry e;
  if(!_sm_find_cl(dcl,fname,&e,false)) return false;
  sm_f_open=2;sm_f_start=e.cluster;sm_f_cur=e.cluster;
  sm_f_sec=0;sm_f_off=0;sm_f_size=e.size;sm_f_pos=0;
  sm_f_dir_lba=e.dir_lba;sm_f_dir_ent=e.dir_ent;
  if(!sm_f_size||sm_f_cur<2) return true;
  return _sm_rblk(sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc);
}
int sm_read(uint8_t*dst,uint8_t len){
  if(sm_f_open!=2) return -1;
  if(sm_f_pos>=sm_f_size) return 0;
  uint8_t n=0;
  while(n<len&&sm_f_pos<sm_f_size){
    dst[n++]=sm_buf[sm_f_off++];sm_f_pos++;
    if(sm_f_off==512){
      sm_f_off=0;sm_f_sec++;
      if(sm_f_sec==sm_spc){
        sm_f_sec=0;uint32_t nx=_sm_fget(sm_f_cur);
        if(_sm_eoc(nx)){sm_f_cur=nx;break;}sm_f_cur=nx;
      }
      if(sm_f_pos<sm_f_size)
        if(!_sm_rblk(sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec)) return -1;
      break;
    }
  }
  return n;
}
void sm_close_r(void){sm_f_open=0;}

// ── Random access (v1.2.5) ───────────────────────────────────────────────────
// sm_seek: 読み取りオープン中のファイルの読み位置を pos へ移動
// ファイル先頭から FAT チェーンを辿るため、シーク自体のコストはクラスタ数に比例
bool sm_seek(uint32_t pos){
  if(sm_f_open!=2) return false;
  if(pos>sm_f_size) pos=sm_f_size;
  uint32_t cl=sm_f_start;
  uint32_t skip=pos/(512UL*sm_spc);
  while(skip--){uint32_t nx=_sm_fget(cl);if(_sm_eoc(nx))return false;cl=nx;}
  sm_f_cur=cl;
  uint32_t rem=pos%(512UL*sm_spc);
  sm_f_sec=(uint8_t)(rem/512);sm_f_off=(uint16_t)(rem%512);sm_f_pos=pos;
  if(pos<sm_f_size)
    if(!_sm_rblk(sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec)) return false;
  return true;
}

// sm_write_at: 既存ファイルの pos に len バイトを部分上書き（その場書き換え）
// - ファイルを開いていない状態でのみ使用可（sm_buf を使うため）
// - 書き込み範囲は 1 セクタ（512B 境界）内に収まること
// - ファイルサイズの拡張は不可（pos+len <= ファイルサイズ）
// - クラスタ解放・ディレクトリ更新を行わないため高速で SD の摩耗も少ない
bool sm_write_at(const char*path,uint32_t pos,const uint8_t*src,uint16_t len){
  if(sm_f_open) return false;
  uint32_t dcl;const char*fname;
  if(!_sm_resolve(path,&dcl,&fname)) return false;
  _SmEntry e;
  if(!_sm_find_cl(dcl,fname,&e,false)) return false;
  if(pos+len>e.size) return false;
  if((pos%512)+len>512) return false;
  uint32_t cl=e.cluster;
  uint32_t skip=pos/(512UL*sm_spc);
  while(skip--){uint32_t nx=_sm_fget(cl);if(_sm_eoc(nx))return false;cl=nx;}
  uint32_t lba=sm_data_lba+(uint32_t)(cl-2)*sm_spc+(uint8_t)((pos/512)%sm_spc);
  if(!_sm_rblk(lba)) return false;
  memcpy(sm_buf+(pos%512),src,len);
  return _sm_wblk(lba);
}

// sm_read_full: セクタ境界をまたいでも len バイト読む
static int sm_read_full(uint8_t* dst, uint8_t len) {
  uint8_t got = 0;
  while (got < len) {
    int n = sm_read(dst + got, (uint8_t)(len - got));
    if (n <= 0) return (n < 0) ? -1 : (int)got;
    got += (uint8_t)n;
  }
  return (int)got;
}

// ── File open for write (create / truncate) ───────────────────────────────────
bool sm_open_w(const char*path){
  if(sm_f_open==2) return false;  // read mode open
  if(sm_f_open==1) sm_f_open=0; // 中断された書き込みをリセット
  uint32_t dcl;const char*fname;
  if(!_sm_resolve(path,&dcl,&fname)) return false;
  _SmEntry e;
  bool found=_sm_find_cl(dcl,fname,&e,false);
  if(found){
    uint32_t cl=e.cluster;
    while(cl>=2&&!_sm_eoc(cl)){uint32_t nx=_sm_fget(cl);_sm_fset(cl,0);cl=nx;}
    if(!_sm_rblk(e.dir_lba)) return false;
    uint8_t*d=sm_buf+e.dir_ent*32;
    d[20]=0;d[21]=0;d[26]=0;d[27]=0;d[28]=0;d[29]=0;d[30]=0;d[31]=0;
    if(!_sm_wblk(e.dir_lba)) return false;
    sm_f_dir_lba=e.dir_lba;sm_f_dir_ent=e.dir_ent;
  } else {
    uint8_t nlen=(uint8_t)strlen(fname);
    uint8_t n_lfn=(nlen+12)/13;
    SmListCtx wctx;
    if(!_sm_dir_free_cl(dcl,n_lfn+1,&wctx)) return false;
    char sfn[11]; _sm_gen_sfn(fname,sfn);
    uint8_t ck=_sm_lfn_cksum(sfn);
    uint32_t wlba=_sm_dir_lba(&wctx);
    if(!_sm_rblk(wlba)) return false;
    for(uint8_t i=0;i<n_lfn;i++){
      _sm_lfn_bld(sm_buf+wctx.ent*32,(uint8_t)(n_lfn-i)|(i==0?0x40:0),fname,nlen,ck);
      if(++wctx.ent==16){
        if(!_sm_wblk(wlba)) return false;
        wlba=_sm_dir_next(&wctx);if(!wlba) return false;
        if(!_sm_rblk(wlba)) return false;
      }
    }
    uint8_t*de=sm_buf+wctx.ent*32;
    memset(de,0,32);memcpy(de,sfn,11);de[11]=0x20;
    if(!_sm_wblk(wlba)) return false;
    sm_f_dir_lba=wlba;sm_f_dir_ent=wctx.ent;
  }
  sm_f_open=1;sm_f_start=0;sm_f_prev=0;sm_f_cur=0;
  sm_f_sec=0;sm_f_off=0;sm_f_pos=0;
  memset(sm_buf,0,512);
  return true;
}
bool sm_write(const uint8_t*src,uint8_t len){
  if(sm_f_open!=1) return false;
  for(uint8_t i=0;i<len;i++){
    if(!sm_f_off&&!sm_f_sec&&!sm_f_cur){
      uint32_t cl=_sm_falloc();if(!cl)return false;
      _sm_fset(cl,0x0FFFFFFFUL);
      if(sm_f_prev)_sm_fset(sm_f_prev,cl);
      sm_f_prev=cl;sm_f_cur=cl;
      if(!sm_f_start){
        sm_f_start=cl;
        if(!_sm_rblk(sm_f_dir_lba))return false;
        _sm_set_clus(sm_buf+sm_f_dir_ent*32,cl);
        if(!_sm_wblk(sm_f_dir_lba))return false;
      }
      memset(sm_buf,0,512);
    }
    sm_buf[sm_f_off++]=src[i];sm_f_pos++;
    if(sm_f_off==512){
      uint32_t lba=sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec;
      if(!_sm_wblk(lba))return false;
      sm_f_off=0;sm_f_sec++;
      if(sm_f_sec==sm_spc){sm_f_sec=0;sm_f_cur=0;}
      memset(sm_buf,0,512);
    }
  }
  return true;
}
bool sm_close_w(void){
  if(sm_f_open!=1) return false;
  if(sm_f_off>0&&sm_f_cur>=2){
    uint32_t lba=sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec;
    _sm_wblk(lba);
  }
  if(!_sm_rblk(sm_f_dir_lba)){sm_f_open=0;return false;}
  uint8_t*d=sm_buf+sm_f_dir_ent*32;
  d[28]=sm_f_pos;d[29]=sm_f_pos>>8;d[30]=sm_f_pos>>16;d[31]=sm_f_pos>>24;
  _sm_wblk(sm_f_dir_lba);
  sm_f_open=0;return true;
}

// ── File open for append (create if not exists) ───────────────────────────────
bool sm_open_a(const char*path){
  if(sm_f_open==2) return false;
  if(sm_f_open==1) sm_f_open=0;
  uint32_t dcl;const char*fname;
  if(!_sm_resolve(path,&dcl,&fname)) return false;
  _SmEntry e;
  if(!_sm_find_cl(dcl,fname,&e,false)) return sm_open_w(path);
  sm_f_open=1;sm_f_dir_lba=e.dir_lba;sm_f_dir_ent=e.dir_ent;sm_f_pos=e.size;
  if(e.size==0||e.cluster<2){
    sm_f_start=0;sm_f_prev=0;sm_f_cur=0;
    sm_f_sec=0;sm_f_off=0;memset(sm_buf,0,512);return true;
  }
  sm_f_start=e.cluster;
  uint32_t cl=e.cluster;
  while(!_sm_eoc(_sm_fget(cl))) cl=_sm_fget(cl);
  uint32_t off_in_cl=e.size%((uint32_t)sm_spc*512);
  if(off_in_cl==0){
    sm_f_prev=cl;sm_f_cur=0;sm_f_sec=0;sm_f_off=0;memset(sm_buf,0,512);return true;
  }
  sm_f_cur=cl;sm_f_prev=cl;
  sm_f_off=(uint16_t)(e.size%512);
  if(sm_f_off>0){
    sm_f_sec=(uint8_t)((off_in_cl-1)/512);
    uint32_t lba=sm_data_lba+(uint32_t)(cl-2)*sm_spc+sm_f_sec;
    if(!_sm_rblk(lba)){sm_f_open=0;return false;}
  } else {
    sm_f_sec=(uint8_t)(off_in_cl/512);memset(sm_buf,0,512);
  }
  return true;
}

// ── File sync (flush partial sector + update dir size, keep file open) ───────
bool sm_sync_w(void){
  if(sm_f_open!=1) return false;
  // 1. Flush current partial sector to SD (sm_buf unchanged by _sm_wblk)
  if(sm_f_off>0&&sm_f_cur>=2){
    uint32_t lba=sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec;
    if(!_sm_wblk(lba)) return false;
  }
  // 2. Update file size in directory entry (sm_buf overwritten with dir sector)
  if(!_sm_rblk(sm_f_dir_lba)) return false;
  uint8_t*d=sm_buf+sm_f_dir_ent*32;
  d[28]=sm_f_pos;d[29]=sm_f_pos>>8;d[30]=sm_f_pos>>16;d[31]=sm_f_pos>>24;
  if(!_sm_wblk(sm_f_dir_lba)) return false;
  // 3. Restore data sector so subsequent sm_write continues correctly
  if(sm_f_off>0&&sm_f_cur>=2){
    uint32_t lba=sm_data_lba+(uint32_t)(sm_f_cur-2)*sm_spc+sm_f_sec;
    if(!_sm_rblk(lba)) return false;
  } else {
    memset(sm_buf,0,512);
  }
  return true;
}

// ── Delete (shared impl) ──────────────────────────────────────────────────────
static bool _sm_del_entry(const char*path,bool want_dir){
  uint32_t dcl;const char*fname;
  if(!_sm_resolve(path,&dcl,&fname)) return false;
  _SmEntry e;
  if(!_sm_find_cl(dcl,fname,&e,want_dir)) return false;
  uint32_t cl=e.cluster;
  while(cl>=2&&!_sm_eoc(cl)){uint32_t nx=_sm_fget(cl);_sm_fset(cl,0);cl=nx;}
  if(!_sm_rblk(e.dir_lba)) return false;
  sm_buf[e.dir_ent*32]=0xE5;
  if(!_sm_wblk(e.dir_lba)) return false;
  for(uint8_t i=0;i<_sm_lfn_cnt;i++){
    if(!_sm_rblk(_sm_lfn_lba[i])) return false;
    sm_buf[_sm_lfn_idx[i]*32]=0xE5;
    if(!_sm_wblk(_sm_lfn_lba[i])) return false;
  }
  return true;
}
bool sm_del(const char*path)   { return _sm_del_entry(path,false); }
bool sm_rmdir(const char*path) { return _sm_del_entry(path,true);  }

// ── Make directory ────────────────────────────────────────────────────────────
bool sm_mkdir(const char*path){
  uint32_t dcl;const char*dname;
  if(!_sm_resolve(path,&dcl,&dname)) return false;
  if(!dname[0]) return false;
  // fail if already exists
  _SmEntry ex;
  if(_sm_find_cl(dcl,dname,&ex,true)) return false;

  uint32_t cl=_sm_falloc();if(!cl) return false;
  _sm_fset(cl,0x0FFFFFFFUL);

  // write . and .. in new cluster
  uint32_t new_lba=sm_data_lba+(uint32_t)(cl-2)*sm_spc;
  memset(sm_buf,0,512);
  memset(sm_buf,' ',11);    sm_buf[0]='.';    sm_buf[11]=0x10; _sm_set_clus(sm_buf,cl);
  memset(sm_buf+32,' ',11); sm_buf[32]='.';sm_buf[33]='.'; sm_buf[43]=0x10;
  uint32_t parent=dcl?dcl:0;
  _sm_set_clus(sm_buf+32,parent);
  if(!_sm_wblk(new_lba)) return false;

  uint8_t nlen=(uint8_t)strlen(dname);
  uint8_t n_lfn=(nlen+12)/13;
  SmListCtx wctx;
  if(!_sm_dir_free_cl(dcl,n_lfn+1,&wctx)) return false;
  char sfn[11]; _sm_gen_sfn(dname,sfn);
  uint8_t ck=_sm_lfn_cksum(sfn);
  uint32_t wlba=_sm_dir_lba(&wctx);
  if(!_sm_rblk(wlba)) return false;
  for(uint8_t i=0;i<n_lfn;i++){
    _sm_lfn_bld(sm_buf+wctx.ent*32,(uint8_t)(n_lfn-i)|(i==0?0x40:0),dname,nlen,ck);
    if(++wctx.ent==16){
      if(!_sm_wblk(wlba)) return false;
      wlba=_sm_dir_next(&wctx);if(!wlba) return false;
      if(!_sm_rblk(wlba)) return false;
    }
  }
  uint8_t*de=sm_buf+wctx.ent*32;
  memset(de,0,32);memcpy(de,sfn,11);de[11]=0x10;
  _sm_set_clus(de,cl);
  return _sm_wblk(wlba);
}

// ── Directory listing ─────────────────────────────────────────────────────────
// Initialize ctx for listing: path="" or NULL = root, otherwise subdirectory path
bool sm_list_open(SmListCtx*ctx,const char*path){
  ctx->ent=0;ctx->sec=0;
  if(!path||!path[0]){ctx->cl=0;return true;}
  uint32_t dcl;const char*dname;
  if(!_sm_resolve(path,&dcl,&dname)) return false;
  _SmEntry e;
  if(!_sm_find_cl(dcl,dname,&e,true)) return false;
  ctx->cl=e.cluster;
  return true;
}
// Returns 1=file, 2=directory, 0=end  (skips . and ..)
int sm_list_next(SmListCtx*ctx,char*name){
  _sm_lfn_rst();
  uint32_t lba=_sm_dir_lba(ctx);
  while(lba){
    if(!_sm_rblk(lba)) return 0;
    while(ctx->ent<16){
      uint8_t*d=sm_buf+ctx->ent*32;
      uint8_t first=d[0];
      if(!first) return 0;
      if(first==0xE5){ctx->ent++;_sm_lfn_rst();continue;}
      if(d[11]==0x0F){_sm_lfn_acc(d,lba,ctx->ent);ctx->ent++;continue;}
      if(d[11]&0x08){ctx->ent++;_sm_lfn_rst();continue;}
      bool is_dir=(d[11]&0x10)!=0;
      if(is_dir&&first=='.'&&!_sm_lfn[0]){ctx->ent++;_sm_lfn_rst();continue;}
      ctx->ent++;
      if(_sm_lfn[0]){
        uint8_t n=0;while(n<26&&_sm_lfn[n]){name[n]=_sm_lfn[n];n++;}name[n]=0;
      } else {
        uint8_t n=0;
        for(uint8_t k=0;k<8&&d[k]!=' ';k++) name[n++]=d[k];
        name[n++]='.';uint8_t ext=0;
        for(uint8_t k=8;k<11;k++) if(d[k]!=' '){name[n++]=d[k];ext++;}
        if(!ext) n--;
        name[n]=0;
      }
      _sm_lfn_rst();
      return is_dir?2:1;
    }
    lba=_sm_dir_next(ctx);
  }
  return 0;
}
