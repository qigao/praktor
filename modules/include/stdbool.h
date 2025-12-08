#ifndef MODULES_STDBOOL_OVERRIDE_H
#define MODULES_STDBOOL_OVERRIDE_H

#ifndef __cplusplus
  #ifndef _Bool
    #define _Bool unsigned char
  #endif
  #ifndef bool
    #define bool _Bool
  #endif
  #ifndef true
    #define true 1
  #endif
  #ifndef false
    #define false 0
  #endif
#endif /* __cplusplus */

#define __bool_true_false_are_defined 1

#endif /* MODULES_STDBOOL_OVERRIDE_H */
