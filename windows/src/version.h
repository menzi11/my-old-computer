// Single source for the version shown in the About box and stamped into the
// executable's version resource (app.rc includes this file too).
#ifndef MOC_VERSION_H
#define MOC_VERSION_H

#define MOC_VERSION_QUAD 2, 9, 18, 0
#define MOC_VERSION_TEXT "2.9.18.0"

#ifndef RC_INVOKED
#define MOC_VERSION_DISPLAY L"2.9.18"
#endif

#endif  // MOC_VERSION_H
