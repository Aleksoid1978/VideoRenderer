#pragma once

#include "../revision.h"

#ifndef REV_DATE
#define REV_DATE       0
#endif

#ifndef REV_HASH
#define REV_HASH       0
#endif

#ifndef REV_NUM
#define REV_NUM        0
#endif

#ifndef REV_BRANCH
#define REV_BRANCH     LOCAL
#endif

#define DO_MAKE_STR(x) #x
#define MAKE_STR(x)    DO_MAKE_STR(x)

#define VER_RELEASE    1

#define VER_MAJOR      0
#define VER_MINOR      9
#define VER_BUILD      13

#define VERSION_NUM    VER_MAJOR,VER_MINOR,VER_BUILD,REV_NUM
#define VERSION_STR    MAKE_STR(VER_MAJOR) "." MAKE_STR(VER_MINOR) "." MAKE_STR(VER_BUILD) "." MAKE_STR(REV_NUM)

#define BRANCH_STR     MAKE_STR(REV_BRANCH)
