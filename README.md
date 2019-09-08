# Cothread: A mix-weight C coroutine lib based on inline assembly

## Introduction

This is a user-space (without system-call) C coroutine lib based on inline assembly.

This lib supports both light-weight coroutine with private stack space (10 KB or so), and middle-weight coroutine which may share a "costack" (2MB ~ 10MB) with others.

User should first create a "cogroup" standing for current kernel-thread (of course, you can have mutiple cogroup in a kernel-thread, but do not make a cogroup be used across kernel-threads), which contains several costacks, and then create "cothreads" in it. The cothread can communicate with others in a same group by "sending" or "yielding".

## API

### User-visible Data Type

</br>

```C
typedef struct cothread_s cothread_t;
```
The cothread's handle.

</br>

```C
typedef enum {
    COTHRD_INIT,
    COTHRD_RUNNING,
    COTHRD_SENDING,
    COTHRD_REPLYING,
    COTHRD_EXITED,
} costate_t;
```
The cothread's state.

</br>

```C
typedef uint64_t codata_t;
```
The data transfered between cothreads.

</br>

```C
typedef void cofunction_t(cothread_t* me, codata_t arg);
```
The function that a cothread enters first.

### Function

```C
cothread_t* cogroup_create(int stack_num, int per_stack_cap);
```
Create a cogroup.

Arguments:
+ `stack_num`: the amount of initial costacks. -1 for default.
+ `per_stack_cap`: the capacity of each costack in the cogroup. -1 for default.

Return:
+ a cothread standing for the cogroup, called main-cothread.

</br>

```C
void cogroup_destroy(cothread_t* main_thd);
```
Destroy the cogroup.

Arguments:
+ `main_thd`: the main-cothread of a cogroup.

Effect:
+ destroy the cogroup of `main_thd`.

Panic if the `main_thd` isn't the main-cothread.

</br>

```C
cothread_t* cothread_create(cothread_t* me, cofunction_t* entry, int is_light_weight);
```
Create a cothread.

Arguments:
+ `me`: current cothread.
+ `entry`: the entry function of the cothread to be created.
+ `is_light_weight`: whether it'll be a light-weight cothread.

Return:
+ a cothread in a same group of `me`.

Panic if `me` isn't current cothread.

</br>

```C
void cothread_destroy(cothread_t* thd);
```
Destroy the cothread.

Panic if it's still running.

</br>

```C
int cothread_send(cothread_t* me, cothread_t* her, codata_t msg, codata_t* reply);
```
Send a message to a cothread.

Will yield CPU and switch to receiver's cothread.

Arguments:
+ `me`: current cothread, as well as the message-sender.
+ `her`: the sending target.
+ `msg`: the message to be sent; if `her` hasn't run, `msg` will be its entry function's argument.
+ `reply`: the place to store the reply.

Return:
+ 0 if success
+ -1 if `her` just exited or has already exited.

Panic if `me` isn't current cothread. 

Panic if `her` is sending a message.

Panic if the two cothreads are not in the same cogroup.

</br>

```C
codata_t cothread_reply(cothread_t* me, codata_t msg);
```
Reply the message to sender.

Will yield CPU and switch to sender's cothread.

Arguments:
+ `me`: current cothread.
+ `msg`: message to be replied.

Return:
+ the message sent from a new sender.

Panic if `me` isn't current cothread.

Panic if `me` doesn't have a sender.

</br>

```C
void cothread_exit(cothread_t* me);
```
Exit current cothread.

Panic if `me` isn't current cothread.

</br>

```C
costate_t cothread_state(cothread_t* me);
cothread_t* cothread_sender(cothread_t* me);
int cothread_is_same_group(cothread_t* me, cothread_t* her);
```
interfaces to get some metadata of cothread.
