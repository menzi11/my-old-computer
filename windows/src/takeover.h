// This program in place of Explorer for opening folders - for this user only.
//
// The shell opens every folder with one command: a drive, a folder on the
// desktop or in a window, and all it keeps as folders too - the Recycle Bin,
// Control Panel's icons, "This PC".  That command is the "open" verb of the
// Folder class in the registry.  Explorer's is there for the machine; this
// program puts its own under the same name for this user, which is the one
// used then, and takes it out again to let Explorer's back.  The machine's is
// never touched.
//
// What the shell hands the command is a folder's path, except for its own
// folders: those come as "::{...}" and the like, and are given to Explorer to
// show (handOver) - as is anything else that is not a folder that is there.
#pragma once

#include <string>

namespace takeover {

// True for what this program shows a window for: a folder that is there.
bool isFolder(const std::wstring& what);

// What is written in the registry for this program: it, and the folder asked for.
std::wstring command();

enum class State {
    explorer,  // nothing is written for this user: Explorer opens folders
    ours,      // written for this program, where it is (or for another copy of it that is there)
    moved,     // written for this program where it was before it was moved: it is not there any more
    other,     // some other program opens folders
};
State state();

// True when it is this program that opens folders (from where it is, or from
// where it was: it is written again for where it is, then).
bool on();

// Writes it for this program, or takes it out (Explorer's is used again).
// False when it could not be done - and when what is written is another
// program's and would be taken out: that is not this program's to remove.
bool set(bool on);

// Written again for where the program is, if it was moved since it was
// written (else a double-click on a folder would start nothing): true if so.
bool heal();

// Gives what is not a folder to Explorer to show.  False when Explorer could
// not be started on it.
bool handOver(const std::wstring& what);

// What Explorer is started with for `what`.
std::wstring explorerParameters(const std::wstring& what);

// (The tests.)  All of this works on `key` instead - under HKEY_CURRENT_USER,
// with the same ending (...\Folder\shell\open\command) - so that nothing
// live is written.  Null: the real one.
void useKey(const wchar_t* key);

}  // namespace takeover
