// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef _UNIT_H_
#define _UNIT_H_

//#include "map.h"
struct block_list;
struct unit_data;
struct map_session_data;

// PC, MOB, PET �ɋ��ʂ��鏈�����P�ɂ܂Ƃ߂�v��

// ���s�J�n
//     �߂�l�́A0 ( ���� ), 1 ( ���s )
int unit_walktoxy( struct block_list *bl, short x, short y, int easy);
int unit_walktobl( struct block_list *bl, struct block_list *target, int range, int easy);
int unit_run(struct block_list *bl);

// ���s��~
// type�͈ȉ��̑g�ݍ��킹 : 
//     1: �ʒu���̑��M( ���̊֐��̌�Ɉʒu���𑗐M����ꍇ�͕s�v )
//     2: �_���[�W�f�B���C�L��
//     4: �s��(MOB�̂݁H)
int unit_stop_walking(struct block_list *bl,int type);
int unit_can_move(struct block_list *bl);
int unit_is_walking(struct block_list *bl);
int unit_set_walkdelay(struct block_list *bl, unsigned int tick, int delay, int type);

int unit_escape(struct block_list *bl, struct block_list *target, short dist);
// �ʒu�̋����ړ�(������΂��Ȃ�)
int unit_movepos(struct block_list *bl, short dst_x, short dst_y, int easy, bool checkpath);
int unit_warp(struct block_list *bl, short map, short x, short y, int type);
int unit_setdir(struct block_list *bl,unsigned char dir);
uint8 unit_getdir(struct block_list *bl);

// �����܂ŕ��s�ł��ǂ蒅���邩�̔���
bool unit_can_reach_pos(struct block_list *bl,int x,int y,int easy);
bool unit_can_reach_bl(struct block_list *bl,struct block_list *tbl, int range, int easy, short *x, short *y);

// �U���֘A
int unit_stop_attack(struct block_list *bl);
int unit_attack(struct block_list *src,int target_id,int continuous);
int unit_cancel_combo(struct block_list *bl);

// �X�L���g�p
int unit_skilluse_id(struct block_list *src, int target_id, short skill_num, short skill_lv);
int unit_skilluse_pos(struct block_list *src, short skill_x, short skill_y, short skill_num, short skill_lv);

// �X�L���g�p( �␳�ς݃L���X�g���ԁA�L�����Z���s�ݒ�t�� )
int unit_skilluse_id2(struct block_list *src, int target_id, short skill_num, short skill_lv, int casttime, int castcancel);
int unit_skilluse_pos2( struct block_list *src, short skill_x, short skill_y, short skill_num, short skill_lv, int casttime, int castcancel);

// �r���L�����Z��
int unit_skillcastcancel(struct block_list *bl,int type);

int unit_counttargeted(struct block_list *bl,int target_lv);

// unit_data �̏���������
void unit_dataset(struct block_list *bl);

int unit_fixdamage(struct block_list *src,struct block_list *target,unsigned int tick,int sdelay,int ddelay,int damage,int div,int type,int damage2);
// ���̑�
struct unit_data* unit_bl2ud(struct block_list *bl);
void unit_remove_map_pc(struct map_session_data *sd, int clrtype);
void unit_free_pc(struct map_session_data *sd);
int unit_remove_map(struct block_list *bl, int clrtype);
int unit_free(struct block_list *bl, int clrtype);
int unit_changeviewsize(struct block_list *bl,short size);

// ���������[�`��
int do_init_unit(void);
int do_final_unit(void);

extern const short dirx[8];
extern const short diry[8];

#endif /* _UNIT_H_ */
