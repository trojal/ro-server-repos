// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _UNIT_H_
#define _UNIT_H_

//#include "map.h"
struct block_list;
struct unit_data;
struct map_session_data;

// PC, MOB, PET に共通する処理を１つにまとめる計画

// 歩行開始
//     戻り値は、0 ( 成功 ), 1 ( 失敗 )
int unit_walktoxy( struct block_list *bl, short x, short y, int easy);
int unit_walktobl( struct block_list *bl, struct block_list *target, int range, int easy);
int unit_run(struct block_list *bl);

// 歩行停止
// typeは以下の組み合わせ : 
//     1: 位置情報の送信( この関数の後に位置情報を送信する場合は不要 )
//     2: ダメージディレイ有り
//     4: 不明(MOBのみ？)
int unit_stop_walking(struct block_list *bl,int type);
int unit_can_move(struct block_list *bl);
int unit_is_walking(struct block_list *bl);
int unit_set_walkdelay(struct block_list *bl, unsigned int tick, int delay, int type);

int unit_escape(struct block_list *bl, struct block_list *target, short dist);
// 位置の強制移動(吹き飛ばしなど)
int unit_movepos(struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath);
int unit_warp(struct block_list *bl, short map, short x, short y, int type);
int unit_setdir(struct block_list *bl,unsigned char dir);
uint8 unit_getdir(struct block_list *bl);

// そこまで歩行でたどり着けるかの判定
bool unit_can_reach_pos(struct block_list *bl,int x,int y,int easy);
bool unit_can_reach_bl(struct block_list *bl,struct block_list *tbl, int range, int easy, short *x, short *y);

// 攻撃関連
int unit_stop_attack(struct block_list *bl);
int unit_attack(struct block_list *src,int target_id,int continuous);
int unit_cancel_combo(struct block_list *bl);

// スキル使用
int unit_skilluse_id(struct block_list *src, int target_id, short skill_num, short skill_lv);
int unit_skilluse_pos(struct block_list *src, short skill_x, short skill_y, short skill_num, short skill_lv);

// スキル使用( 補正済みキャスト時間、キャンセル不可設定付き )
int unit_skilluse_id2(struct block_list *src, int target_id, short skill_num, short skill_lv, int casttime, int castcancel);
int unit_skilluse_pos2( struct block_list *src, short skill_x, short skill_y, short skill_num, short skill_lv, int casttime, int castcancel);

// 詠唱キャンセル
int unit_skillcastcancel(struct block_list *bl,int type);

int unit_counttargeted(struct block_list *bl,int target_lv);

// unit_data の初期化処理
void unit_dataset(struct block_list *bl);

int unit_fixdamage(struct block_list *src,struct block_list *target,unsigned int tick,int sdelay,int ddelay,int damage,int div,int type,int damage2);
// その他
struct unit_data* unit_bl2ud(struct block_list *bl);
void unit_remove_map_pc(struct map_session_data *sd, int clrtype);
void unit_free_pc(struct map_session_data *sd);
int unit_remove_map(struct block_list *bl, int clrtype);
int unit_free(struct block_list *bl, int clrtype);
int unit_changeviewsize(struct block_list *bl,short size);

// 初期化ルーチン
int do_init_unit(void);
int do_final_unit(void);

extern const short dirx[8];
extern const short diry[8];

#endif /* _UNIT_H_ */
