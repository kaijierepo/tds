#ifndef VERSION_H_
#define VERSION_H_

#define SVN_VERSION "$WCREV$"

#if $WCMODS?1:0$
#pragma message("warning: local modification found ,please make sure source is updated,when bulid release package")
#endif

#endif