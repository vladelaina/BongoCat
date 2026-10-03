# Security Policy

## Windows Input and Game Compatibility

The Windows input backend subscribes to keyboard and mouse Raw Input using
`RIDEV_INPUTSINK | RIDEV_DEVNOTIFY` on a message-only window owned by BongoCat.
It preserves legacy input delivery and does not install global input hooks,
inject code into games, read or write game memory, simulate game input, or
install an input driver. The normal Windows runtime requests `asInvoker`
with `uiAccess="false"`; it does not request administrator privileges.

Foreground window and cursor queries provide read-only state for pointer
tracking and local diagnostics. Normal input diagnostics contain counters,
timing, and window/cursor metadata, rather than typed text. Development tools
that simulate input are separate `EXCLUDE_FROM_ALL` targets and are not part
of the application target or its installation rules.

Input diagnostics are enabled in the normal session log. At most once every
10 seconds per running loop, they report cumulative receiver/consumer counters
and a current display snapshot, including the foreground PID and monitor
bounds. They do not log key names, scan codes, text, window titles, process
paths, or device identifiers. Counter updates do not write a log per input
event. Receiver startup/shutdown and registration failures are logged separately.

Registration success alone does not establish that input messages are
arriving. BongoCat does not attempt to bypass a game's input restrictions.
The receiver checks its process-local registrations once per second and
restores missing subscriptions or background flags on its own receiver.
It leaves registrations targeting another receiver untouched. Recovery is
not triggered by a period without input or by the foreground monitor, and
is deferred while the normal input desktop is unavailable.

Using documented Windows APIs does not guarantee acceptance by every
anti-cheat product. The source checks in
`cmake/CheckPlatformRuntimeSafety.cmake` enforce the project's input API
boundaries; they are not an anti-cheat certification or a substitute for
testing the actual release build.

## Linux Input

X11 uses XInput2 by default. Experimental Wayland input through evdev is
disabled unless the process is started with `BONGOCAT_ENABLE_EVDEV=1` in a
Wayland session. Restart without that variable to disable it. This opt-in
selects evdev for the entire session; unreadable devices do not trigger
automatic backend switching. Window placement still depends on the compositor.

The evdev backend opens existing input event devices read-only and never
changes permissions, installs udev rules, requests root, grabs devices, or
injects input. Do not run BongoCat as root or add your account to the
`input` group just to enable this feature. Broad input-group membership or
generic udev rules can also grant other processes under your account access
to your keyboard. Device access must be managed separately by your system
administrator with the narrowest permissions appropriate for the machine.

Raw device input is more sensitive than compositor-mediated input: it can
include keys entered in password fields, and this backend does not detect
screen locking or session changes. Hiding the pet does not stop monitoring.
Do not enable evdev where this scope is unacceptable; exit BongoCat before
locking or switching sessions when using this experimental backend. Removing
a device ACL is not a guarantee that already-open descriptors are revoked.
Key events stay in memory for animation and shortcuts; diagnostics contain
device counts, not key names or input contents. They are not uploaded.

Each device has separate pressed state. Disconnects release its held inputs.
On kernel buffer overflow, known keys are released and incomplete events are
discarded until the next synchronization report. Since the backend deliberately
does not query device state with ioctl, a key held across overflow must be
released and pressed again. Relative mouse motion is raw and unaccelerated,
not the compositor's global cursor position; absolute touchpad motion is not
converted into relative motion.

## Reporting Security Issues

We always aim to ship secure software and take security flaws seriously. Thank you for responsibly disclosing your findings to the team to keep the open source community a safe space.

To report a security issue, reach us at vladelaina@gmail.com. We will be in touch should we need any additional information or guidance to fix the bug.
