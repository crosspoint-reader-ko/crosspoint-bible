#pragma once

// Reboots immediately after an activity releases exclusive raw storage (USB Drive).
// An RTC_NOINIT flag survives the restart, so setup() skips the boot splash and lands
// on home (the Bible reader). The live frontlight state rides along in the same flag
// so the light comes back exactly as it was.
void restartToHomeAfterStorageHandoff();
