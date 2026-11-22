#pragma once

// Appends a line to %TEMP%\moc-diag.txt when MOC_DEBUG is set.  Used by the
// headless verification harness; silent in normal runs.
void diagLog(const wchar_t* fmt, ...);
