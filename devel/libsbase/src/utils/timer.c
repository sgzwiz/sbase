#include <stdio.h>
#include <unistd.h>
#include "timer.h"
#ifdef _DEBUG_TIMER
int main()
{
    void *timer = NULL;
    int i = 0;
    TIMER_INIT(timer);
    if(timer)
    {
        for(i = 0; i < 100000; i++);
        TIMER_SAMPLE(timer);
        fprintf(stdout, "last_sec:%ld last_usec:%lld last_sec_used:%ld last_usec_used:%lld\n",
                PT_SEC(timer), PT_USEC(timer), PT_SEC_USED(timer), PT_USEC_USED(timer));
        TIMER_RESET(timer);
        fprintf(stdout, "last_sec:%ld last_usec:%lld last_sec_used:%ld last_usec_used:%lld\n",
                PT_SEC(timer), PT_USEC(timer), PT_SEC_USED(timer), PT_USEC_USED(timer));
        for(i = 0; i < 10000000; i++);
        TIMER_SAMPLE(timer);
        fprintf(stdout, "last_sec:%ld last_usec:%lld last_sec_used:%ld last_usec_used:%lld\n",
                PT_SEC(timer), PT_USEC(timer), PT_SEC_USED(timer), PT_USEC_USED(timer));
        TIMER_CLEAN(timer);
    }
}
#endif