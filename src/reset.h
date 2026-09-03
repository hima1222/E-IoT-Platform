#pragma once

#include <Arduino.h>

namespace ResetManager {

// Clears only the saved WiFi SSID/password and flips the device back
// to "unconfigured" for boot purposes (OwnershipFlow::hasSavedConfig()
// returns false afterward). Other saved fields (user info, device QR,
// interval) are left alone. Reboots the device afterward so it comes
// back up straight into Section 3's unconfigured boot path.
void softReset();

// Wipes the entire "own_cfg" namespace — WiFi creds AND user info AND
// interval. Reboots afterward.
void hardReset();

}  // namespace ResetManager
