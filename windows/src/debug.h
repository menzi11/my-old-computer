#pragma once

// Appends a line to %TEMP%\moc-diag.txt when MOC_DEBUG is set.  Used by the
// headless verification harness; silent in normal runs.
void diagLog(const wchar_t* fmt, ...);

// Milliseconds on a steady clock, for the timings the log carries.
double diagMillis();

// True for an instance started with MOC_NO_FOREGROUND=1 - a test's, on a
// machine somebody else is using.  Its windows never take the real
// foreground, so no click or key of theirs lands in it.
bool staysInBackground();
