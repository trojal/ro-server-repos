// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#ifndef	_TIMER_H_
#define	_TIMER_H_

#ifndef _CBASETYPES_H_
#include "../common/cbasetypes.h"
#endif

#define DIFF_TICK(a,b) ((int)((a)-(b)))

#define INVALID_TIMER -1

// timer flags
#define TIMER_ONCE_AUTODEL 0x01
#define TIMER_INTERVAL     0x02
#define TIMER_REMOVE_HEAP  0x10

// Struct declaration

typedef int (*TimerFunc)(int tid, unsigned int tick, int id, int data);

struct TimerData {
	unsigned int tick;
	TimerFunc func;
	int type;
	int interval;
	int heap_pos;

	// general-purpose storage
	int id; 
	int data;
};

// Function prototype declaration

unsigned int gettick(void);
unsigned int gettick_nocache(void);

int add_timer(unsigned int tick, TimerFunc func, int id, int data);
int add_timer_interval(unsigned int tick, TimerFunc func, int id, int data, int interval);
struct TimerData* get_timer(int tid);
int delete_timer(int tid, TimerFunc func);

int addtick_timer(int tid, unsigned int tick);
int settick_timer(int tid, unsigned int tick);

int add_timer_func_list(TimerFunc func, char* name);

unsigned long get_uptime(void);
unsigned int calc_times(void);

int do_timer();
void timer_init(void);
void timer_final(void);

#endif /* _TIMER_H_ */
