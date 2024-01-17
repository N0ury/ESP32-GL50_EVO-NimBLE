#include <Arduino.h>
#include "NimBLEDevice.h"
#include "esp_log.h"

#define TAG "client_gl50"

static const uint8_t SCAN_DURATION_S = 60; // in seconds
static const int SCAN_DURATION_MS = SCAN_DURATION_S * 1000; // in milliseconds

// The Glucose service we wish to connect to.
static NimBLEUUID glucose_servUUID("1808");
static NimBLEUUID device_information_servUUID("180A");
// The characteristics of the remote service we are interested in.
static NimBLEUUID    measurement_charUUID("2A18"); // Glucose Measurement
static NimBLEUUID    context_charUUID("2A34"); // Glucose Measurement Context
static NimBLEUUID    racp_charUUID("2A52"); // Record Access Control Point
static NimBLEUUID    serialNum_charUUID("2A25"); // Record Access Control Point

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static boolean doPair = false;
static NimBLEAdvertisedDevice* myDevice;

static boolean isContext = false;

NimBLERemoteCharacteristic* pRemoteChar_racp;
NimBLERemoteCharacteristic* pRemoteChar_serialNum;

static NimBLEClient* pClient;

static uint16_t year;
static uint8_t month;
static uint8_t day;
static uint8_t hour;
static uint8_t minute;
static uint16_t measurement;
char buffer[40];

class MySecurity : public NimBLESecurityCallbacks {
	uint32_t onPassKeyRequest(){
		ESP_LOGD(TAG, "In onPassKeyRequest");
		return 982249;
	}
	void onPassKeyNotify(uint32_t pass_key){
		ESP_LOGD(TAG, "In onPassKeyNotify");
	}
	bool onConfirmPIN(uint32_t pass_key){
		ESP_LOGD(TAG, "In onConfirmPIN");
		return false;
	}
	bool onSecurityRequest(){
		ESP_LOGD(TAG, "In onSecurityRequest");
		return true;
	}
	void onAuthenticationComplete(ble_gap_conn_desc desc){
		ESP_LOGD(TAG, "In onAuthenticationComplete");
	}
	/** Pairing process complete, we can check the results in ble_gap_conn_desc */
	void onAuthenticationComplete(ble_gap_conn_desc* desc){
		if(!desc->sec_state.encrypted) {
			Serial.println("Encrypt connection failed - disconnecting");
			/** Find the client with the connection handle provided in desc */
			NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
			return;
		}
	}
};

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData,
                           size_t length,
                           bool isNotify) {
    ESP_LOGD(TAG, "In notifyCallback");

    if (isNotify) {
      ESP_LOGD(TAG, "This is a notification");
      uint8_t type = (byte)pData[0] >> 4;
      if (type == 1) { // context follows
        year = (pData[4] << 8 | pData[3]);
        month = pData[5];
        day = pData[6];
        hour = pData[7];
        minute = pData[8];
        measurement = (pData[9] << 8 | pData[10]);
        sprintf(buffer,"%d-%02d-%02d %02d:%02d %d """,year, month, day, hour, minute, measurement);
        isContext = true;
      } else if (isContext) {
        uint8_t marquage = pData[3];
        if (marquage == 1)
  	;
        else if (marquage == 2)
  	marquage = 3;
        else
  	marquage = 16;
        sprintf(buffer,"%s %d", buffer, marquage);
        Serial.println(buffer);
        isContext = false;
      } else {
        year = (pData[4] << 8 | pData[3]);
        month = pData[5];
        day = pData[6];
        hour = pData[7];
        minute = pData[8];
        measurement = (pData[9] << 8 | pData[10]);
        sprintf(buffer,"%d-%02d-%02d %02d:%02d %d """,year, month, day, hour, minute, measurement);
        Serial.println(buffer);
      }
    } else {
      ESP_LOGD(TAG, "This is an indication");
      if (pData[0] == 6) {
	// This is a response code
        if (pData[2] == 1 && pData[3] == 1) {
	  //request was "Report stored values" and Response is success
	  // Confirmation to indication
          const uint8_t txValue[] = {0x1e};
          NimBLEUUID serv_UUID = NimBLEUUID("1808");
          NimBLEUUID char_UUID = NimBLEUUID("2A52");
	  NimBLERemoteService* pRemoteService;
	  NimBLERemoteCharacteristic* pRemoteChar;
          pRemoteService = pClient->getService(serv_UUID);
          if (pRemoteService == nullptr) {
            ESP_LOGD(TAG, "Failed to find device information service UUID: %s", serv_UUID.toString().c_str());
            pClient->disconnect();
            return;
          }
          pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
          if(pRemoteChar != nullptr && pRemoteChar->canWrite()) {
            pRemoteChar->writeValue((uint8_t*)txValue, 1, false);
            ESP_LOGD(TAG, "Confirmation to indication sent");
          }
	  Serial.println("Traitement termine avec succes");
	} else {
	  //il y a eu un problème
	  Serial.println("*** Traitement en anomalie ***");
          ESP_LOGD(TAG, "Abnormal Indication: response code: %d, operator: %d, request opcode: %d, response opcode: %d",
			  pData[0], pData[1], pData[2], pData[3]);
	}
      }
    }
}

class MyClientCallback : public NimBLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    ESP_LOGD(TAG, "onDisconnect");
  }
};

bool ConnectCharacteristic(BLERemoteService* pRemoteService, NimBLEUUID l_charUUID) {
    // Obtain a reference to the characteristic in the service of the GL50 EVO
    NimBLERemoteCharacteristic* pRemoteCharacteristic;
    pRemoteCharacteristic = pRemoteService->getCharacteristic(l_charUUID);
    if (pRemoteCharacteristic == nullptr) {
      ESP_LOGD(TAG, "Failed to find our characteristic UUID: %s", l_charUUID.toString().c_str());
      return false;
    }
    ESP_LOGD(TAG, " - Found characteristic: %s - handle: %d", pRemoteCharacteristic->getHandle());

    return true;
}

bool connectToServer() {
    ESP_LOGD(TAG, "Forming a connection to %s", myDevice->getAddress().toString().c_str());

    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityCallbacks(new MySecurity());
    NimBLESecurity *pSecurity = new NimBLESecurity();
    pSecurity->setKeySize();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    pSecurity->setCapability(ESP_IO_CAP_NONE);
    pSecurity->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    pSecurity->setKeySize(16);

    pClient = NimBLEDevice::createClient();
    ESP_LOGD(TAG, " - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the GL50 EVO
    pClient->connect(myDevice);  // if you pass NimBLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    ESP_LOGD(TAG, " - Connected to server");
  
    connected = true;

    return true;
}

// Scan for NimBLE servers and find the first one that advertises the service we are looking for.
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  // Called for each advertising NimBLE server.
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) {
    ESP_LOGD(TAG, "BLE Advertised Device found: %s", advertisedDevice->toString().c_str());
    // We have found a device, let us now see if it's our glucometer
    if (advertisedDevice->haveName() && advertisedDevice->getName() == "Beurer GL50EVO"
        && advertisedDevice->getAddress().toString() == "ed:ac:3e:ea:54:ff") {
      ESP_LOGD(TAG, "Beurer trouve");
      NimBLEDevice::getScan()->stop();
      myDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
      doConnect = true;
      doScan = true;
    } // Found our device (glucometer)
  } // onResult
}; // MyAdvertisedDeviceCallbacks

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Début du traitement");
  NimBLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for SCAN_DURATION_S seconds.
  NimBLEScan* pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  Serial.printf("Recherche du GL50 EVO durant %ld secondes, merci de patienter\n", SCAN_DURATION_S);
  pBLEScan->start(SCAN_DURATION_S, false);


} // End of setup.


void loop() {
  if (doConnect == true) {
    if (connectToServer()) {
      ESP_LOGD(TAG, "We are now connected to the NimBLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  if (connected) {
    ESP_LOGD(TAG, "Yes, connected");
    // On ........
    NimBLERemoteService* pRemoteService;
    NimBLERemoteCharacteristic* pRemoteChar;
    NimBLEUUID serv_UUID;
    NimBLEUUID char_UUID;

    serv_UUID = NimBLEUUID("1801");
    char_UUID = NimBLEUUID("2A05");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find Generic Attribute service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if (pRemoteChar != nullptr && pRemoteChar->canIndicate()) {
      ESP_LOGD(TAG, " - subscribing to indication");
      pRemoteChar->subscribe(false, notifyCallback, true);
    } else {
      ESP_LOGD(TAG, "Failed to set indication on");
    }

    serv_UUID = NimBLEUUID("1808");
    char_UUID = NimBLEUUID("2A52");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find Generic Attribute service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if (pRemoteChar != nullptr && pRemoteChar->canIndicate()) {
      ESP_LOGD(TAG, " - subscribing to indication");
      pRemoteChar->subscribe(false, notifyCallback, true);
    } else {
      ESP_LOGD(TAG, "Failed to set indication on");
    }

    serv_UUID = NimBLEUUID("1808");
    char_UUID = NimBLEUUID("2A34");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find Generic Attribute service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if (pRemoteChar != nullptr && pRemoteChar->canNotify()) {
      ESP_LOGD(TAG, " - subscribing to notification");
      pRemoteChar->subscribe(true, notifyCallback, true);
    } else {
      ESP_LOGD(TAG, "Failed to set notification on service UUID: %s", serv_UUID.toString().c_str());
      while(1) { }
    }

    serv_UUID = NimBLEUUID("1808");
    char_UUID = NimBLEUUID("2A18");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find Generic Attribute service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if (pRemoteChar != nullptr && pRemoteChar->canNotify()) {
      ESP_LOGD(TAG, " - subscribing to notification");
      pRemoteChar->subscribe(true, notifyCallback, true);
    } else {
      ESP_LOGD(TAG, "Failed to set notification on service UUID: %s", serv_UUID.toString().c_str());
      while(1) { }
    }

    serv_UUID = NimBLEUUID("180A");
    char_UUID = NimBLEUUID("2A25");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find generic device information service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if(pRemoteChar != nullptr && pRemoteChar->canRead()) {
      std::string value = pRemoteChar->readValue().c_str();
      ESP_LOGD(TAG, "The Serial Number is: : %s", value.c_str());
    }

    const uint8_t txValue[] = {0x1, 0x1}; // Get All records
    serv_UUID = NimBLEUUID("1808");
    char_UUID = NimBLEUUID("2A52");
    pRemoteService = pClient->getService(serv_UUID);
    if (pRemoteService == nullptr) {
      ESP_LOGD(TAG, "Failed to find device information service UUID: %s", serv_UUID.toString().c_str());
      pClient->disconnect();
      return;
    }
    pRemoteChar = pRemoteService->getCharacteristic(char_UUID);
    if(pRemoteChar != nullptr && pRemoteChar->canWrite()) {
      pRemoteChar->writeValue((uint8_t*)txValue, 2, true);
      ESP_LOGD(TAG, "Asked for all records");
    }
    while(1) { } // Arret
  }else if(doScan){
    NimBLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  } else {
    Serial.println("\nTraitement interrompu, GL50 EVO non détecté.");
    while(1) { } // Arret
  }
  delay(1000); // Delay a second between loops.
}
