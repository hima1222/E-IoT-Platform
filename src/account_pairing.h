#pragma once
 
#include <Arduino.h>
 
namespace AccountPairing {
 
enum class Result {
    CONNECTED,       // paired and WiFi-connected (via AP portal or BLE)
    FAILED,           // credentials submitted but connection failed
    NOT_ATTEMPTED      // reserved for future use (e.g. portal timeout)
};
 
// Fired once pairing resolves, regardless of which transport (WiFi
// portal or BLE) produced the result.
using ResultCallback = void (*)(Result result, const String &connectedSsid);
 
// Call once from setup(). Initializes device identity (MAC/UUID),
// then starts both the WiFi AP portal and BLE pairing simultaneously.
// Whichever transport pairs first wins; the other is torn down.
void begin(ResultCallback onResult);
 
// Call every loop() iteration while pairing is unresolved.
void loop();
 
// True while either transport is still actively waiting for/
// processing a pairing attempt.
bool isPairing();
 
// --- Device identity, exposed for other sections to reuse ---
// (Section 3 needs these for the cloud registration payload,
// Section 10 reports firmware version alongside them, etc.)
//
// initIdentity() sets up MAC + UUID only — no WiFi AP, no BLE. Safe
// to call multiple times (it's idempotent). begin() also calls this
// internally, so call it here directly when you need identity ready
// before deciding whether to open the portal at all (e.g. on a boot
// where saved config means the portal never opens).
void initIdentity();
String getMac();
String getUuid();
String getRegistrationPayload();  // JSON: {"mac":...,"uuid":...,"model":...,"fw":...}
void setModelInfo(const String &model, const String &fwVersion);
 
// Lets another section (Section 3 — Ownership Flow) see the raw JSON
// body of every POST /pair call, to read fields this module doesn't
// care about (user info, device QR choice, interval) without this
// module needing to know what those fields are. Called once, right
// after Section 1 finishes its own parsing, before the HTTP response
// is sent.
using PairBodyListener = void (*)(const String &rawJsonBody);
void setPairBodyListener(PairBodyListener listener);
 
}  // namespace AccountPairing