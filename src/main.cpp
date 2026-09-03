#include <Arduino.h>
#include "config.h"
#include "account_pairing.h"   // Section 1 — Account & Pairing
#include "led_states.h"          // Section 2 — LED States
#include "ownership_flow.h"       // Section 3 — Ownership Flow
#include "on_demand_portal.h"      // Section 4 — On-Demand Portal
#include "reset.h"                  // Section 5 — Reset
#include "fota.h"
#include "edge_smartconfig.h"
#include "health_check.h"
 
void onAccountPairingResult(AccountPairing::Result result, const String &connectedSsid) {
    switch (result) {
        case AccountPairing::Result::CONNECTED:
            DBGLN("[Section 1] Paired + connected to: " + connectedSsid);
            LedStates::setState(LedStates::State::CLOUD_PENDING);
            OwnershipFlow::onFreshPairingConnected(connectedSsid);
            break;
        case AccountPairing::Result::FAILED:
            DBGLN("[Section 1] Pairing attempt failed — reopening.");
            AccountPairing::begin(onAccountPairingResult);
            LedStates::setState(LedStates::State::CONFIG_PORTAL_ACTIVE);
            break;
        case AccountPairing::Result::NOT_ATTEMPTED:
            break;
    }
}
 
void onButtonPress(OnDemandPortal::PressType type) {
    switch (type) {
        case OnDemandPortal::PressType::SHORT:
            DBGLN("[Section 4] Button short-press — reopening portal.");
            AccountPairing::begin(onAccountPairingResult);
            LedStates::setState(LedStates::State::CONFIG_PORTAL_ACTIVE);
            break;
        case OnDemandPortal::PressType::LONG:
            ResetManager::softReset();   // reboots internally
            break;
        case OnDemandPortal::PressType::VERY_LONG:
            ResetManager::hardReset();    // reboots internally
            break;
    }
}
 
void setup() {
    DBG_INIT(115200);
    delay(300);

    Fota::begin();  // logs boot info, handles double-reset rollback, runs self-test/confirm

    HealthCheck::begin();
    HealthCheck::trackTask("TaskFota", Fota::getTaskHandle());
 
    #if USE_EDGE_SMARTCONFIG_VARIANT
        AccountPairing::initIdentity();   // still want stable MAC/UUID under this variant
        EdgeSmartConfig::begin();
    #else
        LedStates::begin();
        AccountPairing::setModelInfo("smart-device-v1", "0.1.0");
        AccountPairing::initIdentity();
        OwnershipFlow::begin();
        OnDemandPortal::begin(BUTTON_PIN, onButtonPress);
 
        DBGLN("=== Device Identity ===");
        DBGLN("MAC:  " + AccountPairing::getMac());
        DBGLN("UUID: " + AccountPairing::getUuid());
        DBGLN("Registration payload (QR content):");
        DBGLN(AccountPairing::getRegistrationPayload());
 
        if (OwnershipFlow::hasSavedConfig()) {
            OwnershipFlow::autoConnectFromSavedConfig();   // skips the portal entirely
        } else {
            AccountPairing::begin(onAccountPairingResult);  // only place begin() is called
            LedStates::setState(LedStates::State::CONFIG_PORTAL_ACTIVE);
        }
    #endif
}
 
void loop() {
    HealthCheck::loop();

    #if USE_EDGE_SMARTCONFIG_VARIANT
        EdgeSmartConfig::loop();
    #else
        AccountPairing::loop();
        LedStates::loop();
        OwnershipFlow::loop();
        OnDemandPortal::loop();
    #endif
}