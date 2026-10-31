#include "display_policy.h"

static const uint16_t timeouts[] = {0, 15, 30, 60, 120, 300};

size_t display_timeout_option(uint16_t seconds)
{
    for(size_t i=0;i<DISPLAY_TIMEOUT_OPTION_COUNT;i++)if(timeouts[i]==seconds)return i;
    return DISPLAY_DEFAULT_TIMEOUT_OPTION;
}
uint16_t display_timeout_seconds(size_t option)
{
    return option<DISPLAY_TIMEOUT_OPTION_COUNT?timeouts[option]:DISPLAY_DEFAULT_TIMEOUT_SECONDS;
}
bool display_timeout_valid(uint16_t seconds)
{
    return display_timeout_seconds(display_timeout_option(seconds))==seconds;
}
void display_policy_init(display_policy_t *p,uint16_t seconds,int64_t now_us)
{
    p->timeout_seconds=display_timeout_valid(seconds)?seconds:DISPLAY_DEFAULT_TIMEOUT_SECONDS;
    p->last_activity_us=now_us;p->screen_off=false;
}
void display_policy_activity(display_policy_t *p,int64_t now_us)
{
    p->last_activity_us=now_us;
}
bool display_policy_tick(display_policy_t *p,int64_t now_us)
{
    /* 仅按单调时钟统计无操作时间；联网校时不会影响熄屏倒计时。 */
    if(now_us<p->last_activity_us) { p->last_activity_us=now_us;return false; }
    if(p->screen_off || !p->timeout_seconds)return false;
    if(now_us-p->last_activity_us<(int64_t)p->timeout_seconds*1000000)return false;
    p->screen_off=true;return true;
}
bool display_policy_wake(display_policy_t *p,int64_t now_us)
{
    bool was_off=p->screen_off;
    p->screen_off=false;p->last_activity_us=now_us;return was_off;
}
bool display_policy_night(int hour,bool valid,bool previous_night)
{
    /* 未校时不能用 1970 年的启动时间冒充当前时间。 */
    if(!valid || hour<0 || hour>23)return previous_night;
    return hour<7 || hour>=19;
}
