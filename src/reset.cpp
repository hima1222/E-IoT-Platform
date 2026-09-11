#include "reset.h"
#include "config.h"
#include <Preferences.h>

namespace ResetManager {

// Clears WiFi creds only — user info/interval survive.
void softReset() {
    Preferences prefs;
    prefs.begin("own_cfg", false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.putBool("configured", false);  // forces the portal path on next boot
    prefs.end();

    DBGLN("[ResetManager] Soft reset done — WiFi creds cleared. Rebooting.");
    delay(200);
    ESP.restart();
}

// Wipes every key in own_cfg — full factory reset of user data.
void hardReset() {
    Preferences prefs;
    prefs.begin("own_cfg", false);
    prefs.clear();  // wipes every key in this namespace: ssid, pass, qr, name, email, mobile, interval, configured
    prefs.end();

    DBGLN("[ResetManager] Hard reset done — all user-configured data erased. Rebooting.");
    delay(200);
    ESP.restart();
}

}  // namespace ResetManager
