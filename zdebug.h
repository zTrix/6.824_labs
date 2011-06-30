#ifndef __ZDEBUG_H__
#define __ZDEBUG_H__

#define DEBUG

#ifdef DEBUG
    #define Z(args...) printf(args)
#else
    #define Z(args...)
#endif

#define F printf("point: %s %d\n", __FILE__, __LINE__)

#endif
