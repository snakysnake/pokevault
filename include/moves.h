#ifndef POKEVAULT_MOVES_H
#define POKEVAULT_MOVES_H

#include <stdint.h>

enum {
    MOVE_STATUS = 1,
    MOVE_PHYSICAL = 2,
    MOVE_SPECIAL = 3
};

typedef struct {
    uint8_t category;
    uint8_t power;    /* 0 when the move has no fixed power */
    uint8_t accuracy; /* 0 when the move does not check accuracy */
    uint8_t pp;
    int8_t priority;
} MoveInfo;

const char *move_name(unsigned move);

/* 0 when the move is empty or outside Gen 5. Stats are Black 2 / White 2. */
int move_info(unsigned move, MoveInfo *out);

const char *move_effect(unsigned move);

#endif
