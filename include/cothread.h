#ifndef __CRZ_COTHREAD_H__
#define __CRZ_COTHREAD_H__

#include <stdint.h>

typedef struct cothread_s cothread_t;
typedef uint64_t codata_t;
typedef enum {
    COTHRD_INIT,
    COTHRD_RUNNING,
    COTHRD_SENDING,
    COTHRD_YIELDING,
    COTHRD_EXITED,
} costate_t;
typedef void cofunction_t(cothread_t* me, codata_t arg);

#define codata_wrap(_x) ((codata_t)(_x))
#define codata_unwrap(_x, _t) ((_t)(_x))

cothread_t* cogroup_create(int stack_num, int per_stack_cap);
void cogroup_destroy(cothread_t* main_thd);

cothread_t* cothread_create(cothread_t* thd, cofunction_t* entry, int is_light_weight);
void cothread_destroy(cothread_t* thd);

int cothread_send(cothread_t* me, cothread_t* her, codata_t msg, codata_t* reply);
codata_t cothread_yield(cothread_t* me, codata_t msg);
void cothread_exit(cothread_t* me);

costate_t cothread_state(cothread_t* me);
cothread_t* cothread_sender(cothread_t* me);
int cothread_is_same_group(cothread_t* me, cothread_t* her);

#endif