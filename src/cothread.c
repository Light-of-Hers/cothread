#include <assert.h>
#include <cothread.h>
#include <string.h>
#include <util.h>

#define KB (1 << 10)
#define MB (1 << 20)

#define LWT_STK_CAP (10 * KB)
#define CTL_STK_CAP (4 * KB)
#define INIT_STK_CAP ((CALLEE_SAVED_REG_NUM + 1) * REG_SIZE)

#define MIN_STK_NUM 4
#define MAX_STK_NUM 16
#define MIN_STK_CAP (2 * MB)
#define MAX_STK_CAP (10 * MB)

#define CALLEE_SAVED_REG_NUM 6
#define REG_SIZE sizeof(uint64_t)

// xjb设一些参数
#define FREQ_UPDATE_PERIOD 100
#define MIN_FREQ_THRESHOLD 20

typedef enum {
    FALSE = 0,
    TRUE = 1,
} bool_t;

typedef struct cogroup_s cogroup_t;
typedef struct costack_s costack_t;

// the constant member will not change after 'cothread_create' return
struct cothread_s {

    cogroup_t* grp; // constant
    costack_t* stk; // constant

    cothread_t* grp_link;
    cothread_t* stk_link;

    struct {
        void* mem;
        int cap;
    } pvt_stk; // constant if the cothread is light-weight

    void* stk_bot; // constant
    void* stk_sp;
    cofunction_t* entry; // constant

    costate_t state;
    cothread_t* sender;
    codata_t msg;
};

// the constant member will not change after 'cogroup_add_stk' return
struct costack_s {

    cogroup_t* grp; // constant

    costack_t* grp_link;

    struct {
        void* mem;
        int cap;
    } pub_stk; // constant

    struct {
        cothread_t* link;
        int num;
    } thds;

    cothread_t* active;

    struct {
        int old;
        int cur;
    } freq;
};

// the constant member will not change after 'cogroup_create' return
struct cogroup_s {

    struct {
        costack_t* link;
        int num;
        int cap;
    } stks;

    struct {
        cothread_t* link;
        int num;
    } thds;

    // main cothread
    cothread_t main; // constant

    // control stack
    struct {
        void* mem;
        int cap;
    } ctrl; // constant

    int time;
};

static void asm_context_switch(cothread_t* me, void** from, void* to);

inline static void* aux_init_stack(void* stk_bot, void* ret_addr);
static void aux_ctrl_switch(cothread_t* to);

inline static costack_t* make_costack(int cap);
inline static void free_costack(costack_t* me);

inline static cothread_t* make_cothread(int is_lw);
inline static void free_cothread(cothread_t* me);

inline static cogroup_t* make_cogroup(int cap, int num);
inline static void free_cogroup(cogroup_t* me);

inline static void costack_add_thd(costack_t* me, cothread_t* thd);
inline static void costack_remove_thd(costack_t* me, cothread_t* thd);
inline static void costack_place_thd(costack_t* me, cothread_t* thd);
inline static void* costack_bottom(costack_t* me);
inline static int costack_freq(costack_t* me);

inline static void cothread_backup_stk(cothread_t* me);
inline static void cothread_restore_stk(cothread_t* me);
inline static void cothread_switch_thd(cothread_t* me, cothread_t* her);
inline static int cothread_in_stk(cothread_t* me);
inline static int cothread_is_lwt(cothread_t* me);
static void cothread_start_exec(cothread_t* me);
inline static void cothread_be_active(cothread_t* me);

inline static costack_t* cogroup_find_stk(cogroup_t* me);
inline static void cogroup_add_thd(cogroup_t* me, cothread_t* thd, int is_lwt);
inline static void cogroup_remove_thd(cogroup_t* me, cothread_t* thd);
inline static void cogroup_add_stk(cogroup_t* me, costack_t* stk);
inline static void cogroup_switch_thds(cogroup_t* me, cothread_t* from, cothread_t* to);
inline static void cogroup_incr_time(cogroup_t* me);

// --------------------
// private methods
// --------------------

// Do a context switch between two user cothreads.
//
// + If the target is a light-weight cothread
// or it's in stack, then just switch to it directly.
// + Otherwise, switch to a specific context to do the
// stack-swapping and the context-switching.
inline static void
cothread_switch_thd(cothread_t* me, cothread_t* her)
{
    if (cothread_is_lwt(her) || cothread_in_stk(her)) {
        asm_context_switch(her, &me->stk_sp, her->stk_sp);
    } else {
        cogroup_switch_thds(me->grp, me, her);
    }
}

// Switch to group's control stack to do the stack-swapping.
inline static void
cogroup_switch_thds(cogroup_t* me, cothread_t* from, cothread_t* to)
{
    void* ctrl_sp = aux_init_stack(me->ctrl.mem + me->ctrl.cap, &aux_ctrl_switch);
    asm_context_switch(to, &from->stk_sp, ctrl_sp);
}

// Do stack-swapping and switch to target cothread.
static void
aux_ctrl_switch(cothread_t* to)
{
    void* junk_sp;

    assert(!cothread_is_lwt(to) && !cothread_in_stk(to));

    costack_place_thd(to->stk, to);
    asm_context_switch(to, &junk_sp, to->stk_sp);
}

// Do stack-swapping.
inline static void
costack_place_thd(costack_t* me, cothread_t* thd)
{
    assert(me->active != thd);

    if (me->active) {
        me->freq.cur++; // every time a cothread is swapped out, record it.
        cothread_backup_stk(me->active);
    }
    cothread_restore_stk(thd);
    me->active = thd;
}

// Backup the cothread's stack data to its private stack.
inline static void
cothread_backup_stk(cothread_t* me)
{
    assert(!cothread_is_lwt(me));

    int cap = me->stk_bot - me->stk_sp;
    me->pvt_stk.mem = my_realloc(me->pvt_stk.mem, cap);
    me->pvt_stk.cap = cap;

    memmove(me->pvt_stk.mem, me->stk_sp, cap);
}

// Restore the cothread's stack data form its private stack.
inline static void
cothread_restore_stk(cothread_t* me)
{
    assert(!cothread_is_lwt(me));
    assert(me->pvt_stk.cap == me->stk_bot - me->stk_sp);

    memmove(me->stk_sp, me->pvt_stk.mem, me->pvt_stk.cap);

    free(me->pvt_stk.mem);
    me->pvt_stk.mem = 0;
    me->pvt_stk.cap = 0;
}

// Initialize the stack for cothread's first execution.
//
// Push the return address (first execution address),
// as well as the callee-saved registers.
inline static void*
aux_init_stack(void* stk_bot, void* ret_addr)
{
    uint64_t* sp = stk_bot;

    sp -= 1;
    *sp = (uint64_t)ret_addr;

    sp -= CALLEE_SAVED_REG_NUM;
    memset(sp, 0, CALLEE_SAVED_REG_NUM * REG_SIZE);

    return sp;
}

// Add the cothread to the costack.
//
// 1. Initialize the cothread's stk_bot, stk_sp.
// 2. Add the cothread's to the costack's link list.
inline static void
costack_add_thd(costack_t* me, cothread_t* thd)
{
    thd->stk = me;
    thd->stk_bot = costack_bottom(me);
    thd->stk_sp = thd->stk_bot - INIT_STK_CAP;

    thd->stk_link = me->thds.link;
    me->thds.link = thd;
    me->thds.num++;
}

// Get the stack bottom of the costack.
inline static void*
costack_bottom(costack_t* me)
{
    return me->pub_stk.mem + me->pub_stk.cap;
}

// Get whether the cothread is in stack.
inline static int
cothread_in_stk(cothread_t* me)
{
    assert(!cothread_is_lwt(me));
    return me->stk->active == me;
}

// Get whether the cothread is light-weight.
inline static int
cothread_is_lwt(cothread_t* me)
{
    return me->stk == NULL;
}

// Add the cothread to the cogroup.
//
// 1. Add the cothread to the cogroup's link list.
// 2. + If the cothread is light-weight, initialize its stk_bot and stk_sp.
//    + Otherwise, find a proper costack to add the cothread.
inline static void
cogroup_add_thd(cogroup_t* me, cothread_t* thd, int is_lwt)
{
    thd->grp = me;
    thd->grp_link = me->thds.link;
    me->thds.link = thd;
    me->thds.num++;

    if (!is_lwt) {
        costack_add_thd(cogroup_find_stk(me), thd);
    } else {
        thd->stk_bot = thd->pvt_stk.mem + thd->pvt_stk.cap;
        thd->stk_sp = thd->stk_bot - INIT_STK_CAP;
    }
}

// Add the costack to the cogroup
//
// Just add the costack to the cogroup's link list.
inline static void
cogroup_add_stk(cogroup_t* me, costack_t* stk)
{
    stk->grp = me;

    stk->grp_link = me->stks.link;
    me->stks.link = stk;
    me->stks.num++;
}

// Alloc a costack.
//
// Alloc its stack memory with the given capacity as well.
inline static costack_t*
make_costack(int cap)
{
    costack_t* stk = my_calloc(1, sizeof(costack_t));
    stk->pub_stk.mem = my_malloc(cap);
    stk->pub_stk.cap = cap;
    return stk;
}

// Free the costack.
inline static void
free_costack(costack_t* me)
{
    free(me->pub_stk.mem);
    memset(me, 0, sizeof(costack_t));
    free(me);
}

// Alloc a cothread.
//
// Alloc its private stack as well.
inline static cothread_t*
make_cothread(int is_lw)
{
    cothread_t* thd = my_calloc(1, sizeof(cothread_t));
    int cap = is_lw ? LWT_STK_CAP : INIT_STK_CAP;
    thd->pvt_stk.mem = my_malloc(cap);
    thd->pvt_stk.cap = cap;
    return thd;
}

// Free the cothread.
inline static void
free_cothread(cothread_t* me)
{
    if (me->pvt_stk.mem)
        free(me->pvt_stk.mem);
    memset(me, 0, sizeof(cothread_t));
    free(me);
}

// Alloc a cogroup.
//
// Alloc its control stack.
// Alloc and add some costacks.
inline static cogroup_t*
make_cogroup(int cap, int num)
{
    cogroup_t* grp = my_calloc(1, sizeof(cogroup_t));

    grp->ctrl.mem = my_malloc(CTL_STK_CAP);
    grp->ctrl.cap = CTL_STK_CAP;

    grp->stks.cap = cap;
    while (num--)
        cogroup_add_stk(grp, make_costack(cap));

    return grp;
}

// Free a cogroup.
//
// Free all its cothreads and costacks as well.
inline static void
free_cogroup(cogroup_t* me)
{
    for (cothread_t* thd = me->thds.link; thd; thd = thd->grp_link)
        free_cothread(thd);
    for (costack_t* stk = me->stks.link; stk; stk = stk->grp_link)
        free_costack(stk);
    if (me->ctrl.mem)
        free(me->ctrl.mem);
    memset(me, 0, sizeof(cogroup_t));
    free(me);
}

// The initial entry of all the cothreads.
static void
cothread_start_exec(cothread_t* me)
{
    cothread_be_active(me);
    me->entry(me, me->msg);
    cothread_exit(me);
}

// Remove the cothread from the costack.
inline static void
costack_remove_thd(costack_t* me, cothread_t* thd)
{
    assert(thd->stk == me);

    if (me->active == thd)
        me->active = NULL;

    if (me->thds.link == thd) {
        me->thds.link = thd->stk_link;
    } else {
        cothread_t* pre = me->thds.link;
        while (pre->stk_link && pre->stk_link != thd)
            pre = pre->stk_link;
        assert(pre->stk_link);
        pre->stk_link = thd->stk_link;
    }
    me->thds.num--;
}

// Remove the cothread from the cogroup.
inline static void
cogroup_remove_thd(cogroup_t* me, cothread_t* thd)
{
    assert(thd->grp == me);
    if (me->thds.link == thd) {
        me->thds.link = thd->grp_link;
    } else {
        cothread_t* pre = me->thds.link;
        while (pre->grp_link && pre->grp_link != thd)
            pre = pre->grp_link;
        assert(pre->grp_link);
        pre->grp_link = thd->grp_link;
    }
    me->thds.num--;
    if (!cothread_is_lwt(thd))
        costack_remove_thd(thd->stk, thd);
}

// Get the recent swapping frequency of the costack.
inline static int
costack_freq(costack_t* me)
{
    return (me->freq.old + me->freq.cur) / 2;
}

// Find the proper costack to store a new cothread.
inline static costack_t*
cogroup_find_stk(cogroup_t* me)
{
    costack_t* best_stk = NULL;
    int min_wt = INF;

    for (costack_t* stk = me->stks.link; stk; stk = stk->grp_link) {
        int wt = costack_freq(stk) + stk->thds.num;
        if (wt < min_wt)
            min_wt = wt, best_stk = stk;
    }

    if (min_wt > MIN_FREQ_THRESHOLD && me->stks.num < MAX_STK_NUM) {
        best_stk = make_costack(me->stks.cap);
        cogroup_add_stk(me, best_stk);
    }

    return best_stk;
}

// Increment current logical time.
//
// Aging the costack's swapping frequency periodically.
inline static void
cogroup_incr_time(cogroup_t* me)
{
    if (++me->time > FREQ_UPDATE_PERIOD) {
        me->time = 0;
        for (costack_t* stk = me->stks.link; stk; stk = stk->grp_link)
            stk->freq.old = costack_freq(stk), stk->freq.cur = 0;
    }
}

// Every time a cothread begins/resumes running, call it.
inline static void
cothread_be_active(cothread_t* me)
{
    me->state = COTHRD_RUNNING;
    cogroup_incr_time(me->grp);
}

// The core part of context switch.

// rdi: cothread_t* me, rsi: void** from, rdx: void* to
__asm__(
    "asm_context_switch:\n"
    "pushq  %rbp\n"
    "pushq  %rbx\n"
    "pushq  %r12\n"
    "pushq  %r13\n"
    "pushq  %r14\n"
    "pushq  %r15\n"

    "movq   %rsp,   (%rsi)\n"
    "movq   %rdx,   %rsp\n"

    "popq   %r15\n"
    "popq   %r14\n"
    "popq   %r13\n"
    "popq   %r12\n"
    "popq   %rbx\n"
    "popq   %rbp\n"
    "ret\n");

// ---------------------------
// API
// ---------------------------

#define MUST_CURRENT(_me)               \
    if ((_me)->state != COTHRD_RUNNING) \
        panic("must be current cothread\n");

cothread_t*
cogroup_create(int num, int cap)
{
    num = MIN(MAX(num, MIN_STK_NUM), MAX_STK_NUM);
    cap = MIN(MAX(cap, MIN_STK_CAP), MAX_STK_CAP);

    cogroup_t* grp = make_cogroup(cap, num);

    grp->main.grp = grp;
    grp->main.state = COTHRD_RUNNING;

    return &grp->main;
}

void cogroup_destroy(cothread_t* thd)
{
    MUST_CURRENT(thd);
    if (thd != &thd->grp->main)
        panic("only main cothread can destroy the group\n");

    free_cogroup(thd->grp);
}

cothread_t*
cothread_create(cothread_t* me, cofunction_t* entry, int is_light_weight)
{
    MUST_CURRENT(me);
    cogroup_t* grp = me->grp;
    cothread_t* thd = make_cothread(is_light_weight);
    cogroup_add_thd(grp, thd, is_light_weight);
    aux_init_stack(thd->pvt_stk.mem + thd->pvt_stk.cap, &cothread_start_exec);
    thd->state = COTHRD_INIT;
    thd->entry = entry;
    return thd;
}

void cothread_destroy(cothread_t* thd)
{
    if (thd->state == COTHRD_RUNNING)
        panic("destroy a running cothread\n");
    cogroup_remove_thd(thd->grp, thd);
    free_cothread(thd);
}

int cothread_send(cothread_t* me, cothread_t* her, codata_t msg, codata_t* reply)
{
    MUST_CURRENT(me);
    if (her->state == COTHRD_EXITED)
        return -1;
    if (her->grp != me->grp)
        panic("send message to other group\n");
    if (her->state == COTHRD_SENDING)
        panic("send message to a sending-cothread\n");

    her->msg = msg;
    her->sender = me;
    me->state = COTHRD_SENDING;
    cothread_switch_thd(me, her);
    cothread_be_active(me);
    her->sender = NULL;
    if (her->state == COTHRD_EXITED)
        return -1;
    if (reply)
        *reply = me->msg;

    return 0;
}

codata_t
cothread_reply(cothread_t* me, codata_t msg)
{
    MUST_CURRENT(me);
    if (me->sender == NULL)
        panic("there's no sender\n");

    cothread_t* her = me->sender;

    her->msg = msg;
    me->state = COTHRD_REPLYING;
    cothread_switch_thd(me, her);
    cothread_be_active(me);
    return me->msg;
}

void cothread_exit(cothread_t* me)
{
    MUST_CURRENT(me);
    if (me->sender == NULL)
        panic("there's no sender\n");

    cothread_t* her = me->sender;

    me->state = COTHRD_EXITED;
    cothread_switch_thd(me, her);
    panic("I've exited!!!\n");
}

costate_t
cothread_state(cothread_t* me)
{
    return me->state;
}

cothread_t*
cothread_sender(cothread_t* me)
{
    return me->sender;
}

int cothread_is_same_group(cothread_t* me, cothread_t* her)
{
    return me->grp == her->grp;
}