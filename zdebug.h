#ifndef __ZDEBUG_H__
#define __ZDEBUG_H__

#define DEBUG

#ifdef DEBUG
    #define Z(args...) printf(" [ ZZ ] "args)
    #define ERR(args...) printf(" [ EE ] %s: %s: %d >>> ", __FILE__, __FUNCTION__, __LINE__);printf(args);printf("\n")
#else
    #define Z(args...)
    #define ERR(args...)
#endif

#define F printf("point: %s %d\n", __FILE__, __LINE__)

#endif
