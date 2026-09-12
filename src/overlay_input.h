#pragma once

namespace hq::overlay_input {
// The callback observes the existing dialog lifecycle; it must remain valid
// until restore(). Installation and dialog transitions retain caller locking.
void install(bool (*dialog_open)());
void restore();
void begin_input();
void begin_cursor();
void end_cursor();
}
