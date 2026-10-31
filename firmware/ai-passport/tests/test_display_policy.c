#include <assert.h>
#include "display_policy.h"

int main(void)
{
    const uint16_t options[]={0,15,30,60,120,300};
    for(size_t i=0;i<DISPLAY_TIMEOUT_OPTION_COUNT;i++) {
        assert(display_timeout_seconds(i)==options[i]);
        assert(display_timeout_option(options[i])==i);
        assert(display_timeout_valid(options[i]));
        display_policy_t p;display_policy_init(&p,options[i],100);
        assert(!p.screen_off);
        if(options[i]) {
            int64_t deadline=100+(int64_t)options[i]*1000000;
            assert(!display_policy_tick(&p,deadline-1));
            assert(display_policy_tick(&p,deadline) && p.screen_off);
            assert(!display_policy_tick(&p,deadline+1));
            display_policy_activity(&p,deadline+2);assert(p.screen_off);
            assert(display_policy_wake(&p,deadline+3) && !p.screen_off);
            assert(!display_policy_tick(&p,deadline+3+(int64_t)options[i]*1000000-1));
            assert(display_policy_tick(&p,deadline+3+(int64_t)options[i]*1000000));
        } else assert(!display_policy_tick(&p,INT64_C(1000000000000)));
    }
    assert(!display_timeout_valid(1) && !display_timeout_valid(UINT16_MAX));
    assert(display_timeout_seconds(999)==60);
    display_policy_t p;display_policy_init(&p,1,0);assert(p.timeout_seconds==60);
    display_policy_activity(&p,59000000);assert(!display_policy_tick(&p,60000000));
    assert(!display_policy_tick(&p,118999999));assert(display_policy_tick(&p,119000000));
    assert(display_policy_wake(&p,120000000));assert(!display_policy_wake(&p,121000000));
    display_policy_activity(&p,500);assert(!display_policy_tick(&p,100));
    assert(p.last_activity_us==100 && !p.screen_off);
    for(int hour=0;hour<24;hour++) {
        assert(display_policy_night(hour,true,false)==(hour<7 || hour>=19));
        assert(display_policy_night(hour,false,false)==false);
        assert(display_policy_night(hour,false,true)==true);
    }
    assert(!display_policy_night(-1,true,false));
    assert(display_policy_night(24,true,true));
    return 0;
}
