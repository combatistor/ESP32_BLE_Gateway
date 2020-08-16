
/** NimBLE_Server Demo:
 *
 *  Demonstrates many of the available features of the NimBLE client library.
 *  
 *  Created: on March 24 2020
 *      Author: H2zero
 * 
*/

#include <NimBLEDevice.h>

void scanEndedCB(NimBLEScanResults results);

static NimBLEAdvertisedDevice* advDevice;

static bool doConnect = false;
static uint32_t scanTime = 0; /** 0 = scan forever */


/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */  
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("Connected");
        /** After connection we should change the parameters if we don't need fast response times.
         *  These settings are 150ms interval, 0 latency, 450ms timout. 
         *  Timeout should be a multiple of the interval, minimum is 100ms.
         *  I find a multiple of 3-5 * the interval works best for quick response/reconnect.
         *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0 latency, 60 * 10ms = 600ms timeout 
         */
        pClient->updateConnParams(120,120,0,60);
    };

    void onDisconnect(NimBLEClient* pClient) {
        Serial.print(pClient->getPeerAddress().toString().c_str());
        Serial.println(" Disconnected - Starting scan");
        NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    };
    
    /** Called when the peripheral requests a change to the connection parameters.
     *  Return true to accept and apply them or false to reject and keep 
     *  the currently used parameters. Default will return true.
     */
    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
        if(params->itvl_min < 24) { /** 1.25ms units */
            return false;
        } else if(params->itvl_max > 40) { /** 1.25ms units */
            return false;
        } else if(params->latency > 2) { /** Number of intervals allowed to skip */
            return false;
        } else if(params->supervision_timeout > 100) { /** 10ms units */
            return false;
        }

        return true;
    };
  
    /********************* Security handled here **********************
    ****** Note: these are the same return values as defaults ********/
    uint32_t onPassKeyRequest(){
        Serial.println("Client Passkey Request");
        /** return the passkey to send to the server */
        return 123456;
    };

    bool onConfirmPIN(uint32_t pass_key){
        Serial.print("The passkey YES/NO number: ");
        Serial.println(pass_key);
    /** Return false if passkeys don't match. */
        return true;
    };

    /** Pairing process complete, we can check the results in ble_gap_conn_desc */
    void onAuthenticationComplete(ble_gap_conn_desc* desc){
        if(!desc->sec_state.encrypted) {
            Serial.println("Encrypt connection failed - disconnecting");
            /** Find the client with the connection handle provided in desc */
            NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
            return;
        }
    };
};


/** Define a class to handle the callbacks when advertisments are received */
class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice->toString().c_str());
        if(advertisedDevice->isAdvertisingService(NimBLEUUID("cba20d00-224d-11e6-9fb8-0002a5d5c51b")))
        {
            Serial.println("Found Our Service");
            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();
            /** Save the device reference in a global for the client to use*/ 
            advDevice = advertisedDevice;
            /** Ready to connect now */ 
            doConnect = true;
        }
    };
};


/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
    std::string str = (isNotify == true) ? "Notification" : "Indication"; 
    str += " from ";
    /** NimBLEAddress and NimBLEUUID have std::string operators */
    str += std::string(pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress());
    str += ": Service = " + std::string(pRemoteCharacteristic->getRemoteService()->getUUID());
    str += ", Characteristic = " + std::string(pRemoteCharacteristic->getUUID());
    
//    str += ", Size = " ;
//    char strLength[sizeof(size_t)]; /* In fact not necessary as snprintf() adds the 0-terminator. */
//    snprintf(strLength, sizeof strLength, "%zu", length);
//    str += strLength;

    //str += ", Value = " + std::string((char*)pData, length);
    str += ", Value = ";
    char hexFormatted[length * 2] = "";
    for(int i = 0; i < length; i++){
      sprintf( hexFormatted, "%s%02X", hexFormatted, pData[i]);
    }
    str+= hexFormatted;
    Serial.println(str.c_str());
    if(pData[0] == 1){
      int battLevel = pData[1];
      Serial.print("Battery level: ");
      Serial.println(battLevel);

      float fwVersion = pData[2] / 10.0;
      Serial.print("Fw version: ");
      Serial.println(fwVersion);

      int timersNumber = pData[8];
      Serial.print("Number of timers: ");
      Serial.println(timersNumber);

      bool dualStateMode = bitRead(pData[9], 4) ;
      str = (dualStateMode == true) ? "Switch mode" : "Press mode"; 
      Serial.println(str.c_str());

      bool inverted = bitRead(pData[9], 0) ;
      str = (inverted == true) ? "Inverted" : "Not inverted"; 
      Serial.println(str.c_str());

      int holdSecs = pData[10];
      Serial.print("Hold seconds: ");
      Serial.println(holdSecs);
    }
}

/** Callback to process the results of the last scan or restart it */
void scanEndedCB(NimBLEScanResults results){
    Serial.println("Scan Ended");
}


/** Create a single global instance of the callback class to be used by all clients */
static ClientCallbacks clientCB;


/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToServer() {
    NimBLEClient* pClient = nullptr;
    
    /** Check if we have a client we should reuse first **/
    if(NimBLEDevice::getClientListSize()) {
        /** Special case when we already know this device, we send false as the 
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if(pClient){
            if(!pClient->connect(advDevice, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        } 
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
        else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }
    
    /** No client to reuse? Create a new one. */
    if(!pClient) {
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max clients reached - no more connections available");
            return false;
        }
        
        pClient = NimBLEDevice::createClient();
        
        Serial.println("New client created");
    
        pClient->setClientCallbacks(&clientCB, false);
        /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout. 
         *  These settings are safe for 3 clients to connect reliably, can go faster if you have less 
         *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
         *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout 
         */
        pClient->setConnectionParams(12,12,0,51);
        /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
        pClient->setConnectTimeout(5);
        

        if (!pClient->connect(advDevice)) {
            /** Created a client but failed to connect, don't need to keep it as it has no data */
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }         
    
    if(!pClient->isConnected()) {
        if (!pClient->connect(advDevice)) {
            Serial.println("Failed to connect");
            return false;
        }
    }
    
    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(pClient->getRssi());
    
    /** Now we can read/write/subscribe the charateristics of the services we are interested in */
    NimBLERemoteService* pSvc = nullptr;
    NimBLERemoteCharacteristic* pChr = nullptr;
    NimBLERemoteDescriptor* pDsc = nullptr;
    
    pSvc = pClient->getService((uint16_t) 0x1800); // GENERIC ACCESS service
    if(pSvc) {     /** make sure it's not null */
        pChr = pSvc->getCharacteristic((uint16_t) 0x2a00); // DEVICE NAME characteristic
    }

    if(pChr) {     /** make sure it's not null */
        if(pChr->canRead()) {
            Serial.print(pChr->getUUID().toString().c_str());
            Serial.print(" Value: ");
            Serial.println(pChr->readValue().c_str()); // should return WoHand
        }  
    }
    
    else{
        Serial.println("GENERIC ACCESS service not found.");
    }
    
    pSvc = pClient->getService("cba20d00-224d-11e6-9fb8-0002a5d5c51b"); // custom device service

    if(pSvc) {     /** make sure it's not null */
        pChr = pSvc->getCharacteristic("cba20003-224d-11e6-9fb8-0002a5d5c51b"); // custom characteristic to notify
    }

    if(pChr) {     /** make sure it's not null */        
        if(pChr->canNotify()) {
            //if(!pChr->registerForNotify(notifyCB)) {
            if(!pChr->subscribe(true, notifyCB)) {
                /** Disconnect if subscribe failed */ 
                pClient->disconnect();
                return false;
            }
        }  
    }        

    else{
        Serial.println("CUSTOM notify service not found.");
    }
    
    if(pSvc) {     /** make sure it's not null */
        pChr = pSvc->getCharacteristic("cba20002-224d-11e6-9fb8-0002a5d5c51b"); // custom characteristic to write
    }

    if(pChr) {     /** make sure it's not null */        
        if(pChr->canWrite()) {
            byte bArray[] = {0x57, 0x02}; // write to get settings of device
            if(pChr->writeValue(bArray, 2)) {
                Serial.print("Wrote new value to: ");
                Serial.println(pChr->getUUID().toString().c_str());
            }
            else {
                /** Disconnect if write failed */ 
                pClient->disconnect();
                return false;
            }
        }  
    }        

    else{
        Serial.println("CUSTOM write service not found.");
    }

    if(pSvc) {     /** make sure it's not null */
        pChr = pSvc->getCharacteristic("cba20002-224d-11e6-9fb8-0002a5d5c51b"); // custom characteristic to write
    }

    if(pChr) {     /** make sure it's not null */        
        if(pChr->canWrite()) {
            byte bArray[] = {0x57, 0x01}; // write to get trigger the device (press mode)
            //byte bArray[] = {0x57, 0x01, 0x01}; // write to get trigger the device (switch mode ON)
            //byte bArray[] = {0x57, 0x01, 0x02}; // write to get trigger the device (switch mode OFF)
            if(pChr->writeValue(bArray, 2)) {
                Serial.print("Wrote new value to: ");
                Serial.println(pChr->getUUID().toString().c_str());
            }
            else {
                /** Disconnect if write failed */ 
                pClient->disconnect();
                return false;
            }
        }  
    }        

    else{
        Serial.println("CUSTOM write service not found.");
    }
    
    Serial.println("Done with this device!");
    return true;
}

void setup (){
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client");
    /** Initialize NimBLE, no device name spcified as we are not advertising */
    NimBLEDevice::init("");
    
    /** Set the IO capabilities of the device, each option will trigger a different pairing method.
     *  BLE_HS_IO_KEYBOARD_ONLY    - Passkey pairing
     *  BLE_HS_IO_DISPLAY_YESNO   - Numeric comparison pairing
     *  BLE_HS_IO_NO_INPUT_OUTPUT - DEFAULT setting - just works pairing
     */
    //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY); // use passkey
    //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); //use numeric comparison
  
    /** 2 different ways to set security - both calls achieve the same result.
     *  no bonding, no man in the middle protection, secure connections.
     *     
     *  These are the default values, only shown here for demonstration.   
     */ 
    //NimBLEDevice::setSecurityAuth(false, false, true); 
    NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);
  
    /** Optional: set the transmit power, default is 3db */
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
    
    /** Optional: set any devices you don't want to get advertisments from */
    // NimBLEDevice::addIgnored(NimBLEAddress ("aa:bb:cc:dd:ee:ff")); 
  
    /** create new scan */  
    NimBLEScan* pScan = NimBLEDevice::getScan(); 
    
    /** create a callback that gets called when advertisers are found */
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    
    /** Set scan interval (how often) and window (how long) in milliseconds */
    pScan->setInterval(45);
    pScan->setWindow(15);
    
    /** Active scan will gather scan response data from advertisers
     *  but will use more energy from both devices
     */
    pScan->setActiveScan(true);
    /** Start scanning for advertisers for the scan time specified (in seconds) 0 = forever
     *  Optional callback for when scanning stops. 
     */
    pScan->start(scanTime, scanEndedCB);
}


void loop (){
    /** Loop here until we find a device we want to connect to */
    while(!doConnect){
        delay(1);
    }
    
    doConnect = false;
    
    /** Found a device we want to connect to, do it now */
    if(connectToServer()) {
        Serial.println("Success! we should now be getting notifications, scanning for more!");
    } else {
        Serial.println("Failed to connect, starting scan");
    }
    
    NimBLEDevice::getScan()->start(scanTime,scanEndedCB);
}
