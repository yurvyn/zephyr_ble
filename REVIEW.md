# Review file for comments and questions regarding the implementation of the coding challenge.

## Mem cache comments

- Mem cache is not a queue. It drops new messages and just retains old ones, why did you go with such design?
- Why not use a existing implementation of zephyr's queue or message queues datastructure?
- You are using basically a circular buffer, but there is no check for NULL pointer
    and blocking for K_FOREVER is not the best.

## BLE comnments

- When reconnecting is the memory cache is not consumed

## General comments
- Code is well structured and very well documented
- Ok understanding of zephyr's scheduling primitives like k_timer, k_mutex
- Good use of SYS_INIT although there is no handling for when a module fails to inits
- Good use of zephyr's LOGGING subsys

## Issues

- Scheduling mem_cache_pop and mem_cache_push in an ISR context of a timer handler.
Should use either the main workqueue thread's scheduling primitives K_WORK_DEFINE
or using zephyr's k_msgq which is ISR safe. Using a MUTEX in ISRs is a no go.

## Nitpicks
- When sending sample failed sampled is pushed on top of queue losing order.
