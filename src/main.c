#include <cothread.h>
#include <stdio.h>

double fact(int n)
{
    return n == 0 ? 1 : (double)n * fact(n - 1);
}

void counter(cothread_t* me, codata_t arg)
{
    int n = codata_unwrap(arg, int);
    double f;
    for (int i = 0; i < n; ++i) {
        f = fact(i);
        cothread_yield(me, codata_wrap(&f));
    }
}

void sender(cothread_t* me, codata_t arg)
{
    int n = codata_unwrap(arg, int);
    if (n > 0) {
        cothread_t* thd = cothread_create(me, sender, n % 2);
        cothread_send(me, thd, codata_wrap(n - 1), NULL);
        cothread_destroy(thd);
    }
    printf("sender: %d\n", n);
}

int main()
{
    cothread_t *me, *thd;
    me = cogroup_create(-1, -1);
    thd = cothread_create(me, counter, 0);
    codata_t get;
    while (cothread_send(me, thd, codata_wrap(100), &get) == 0)
        printf("%F\n", *codata_unwrap(get, double*));
    cothread_destroy(thd);

    thd = cothread_create(me, sender, 0);
    cothread_send(me, thd, codata_wrap(1000), NULL);
    cothread_destroy(thd);

    cogroup_destroy(me);
}