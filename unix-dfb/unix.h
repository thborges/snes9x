#ifndef _UNIX_H
#define _UNIX_H

#define DFBCHECK(x...) {                                    \
    DFBResult err = x;                                      \
    if (err != DFB_OK) {                                    \
      fprintf(stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
      DirectFBErrorFatal(#x, err);                          \
    }                                                       \
}

#endif
