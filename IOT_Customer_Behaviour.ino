#include <Arduino.h>
#include <ESP8266WiFiMulti.h>
//#include <ArduinoJson.h>
//#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Adafruit_SSD1306.h>
#include "SFDCPlatformEventClient.h"
#include "CustomerVisit.h"

#define OLED_RESET D4
#define USE_SERIAL Serial
#define CARD_BEEP_PIN D2 //GPIO4
#define BUTTON_PIN D1 //GPIO5
#define SS_PIN D8
#define RST_PIN D3
#define STATE_SCAN_CLIENT_CARD 0
#define STATE_SCAN_PRODUCTS 1
#define STATE_CHECKOUT 2

Adafruit_SSD1306 display(OLED_RESET);

/******** Customer numbers:
 * 3789381763
 * 212975449
 * 252886873
 * 29538606
 */

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance.
ESP8266WiFiMulti WiFiMulti;
SFDCPlatformEventClient sfdcPEClient;

char barcode[128];
int i = 0;
String readString;
volatile byte state;
volatile bool changeStateFlag;

//WiFi credentials
const char* ssid = "Pawel-iPhone";
const char* pass = "9to9u1kfpm1be";

//SFDC parameters
const char* sfdc_client_id =
		"3MVG9I5UQ_0k_hTkAPIld6q7UhbaLvBiFjDZ67.bGTeJjH24COcQztfqVPS.Tv5JLjpuiZjvHgGZpPWzSvhGc";
const char* sfdc_secret_id = "492835174303365953";
const char* sfdc_user = "iot@pwc.com.iot";
const char* sfdc_password = "Esp82662018";
const char* sfdc_token = "sM18sy5Ep6g1Yddvp6a6ZqzOO";

bool customerCardReadFlag = false;
bool productReadFlag = false;
bool endProductReading = false;
char instanceURL[256];
char token[512];
char customerNumber[20];

void setup() {
	pinMode(BUTTON_PIN, INPUT_PULLUP);
	attachInterrupt(BUTTON_PIN, handleKey, FALLING);
	pinMode(CARD_BEEP_PIN, OUTPUT);
	digitalWrite(CARD_BEEP_PIN, HIGH);

	digitalWrite(CARD_BEEP_PIN, LOW);
	delay(100);
	digitalWrite(CARD_BEEP_PIN, HIGH);

	state = STATE_SCAN_CLIENT_CARD;

	Serial.begin(38400); // Initialize serial communications with the PC

	// Configure LCD

	display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false);
	//display.setRotation(2);

	// Clear the buffer.
	display.clearDisplay();
	display.display();

	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
	display.display();

	WiFiMulti.addAP(ssid, pass);

	// Wait for successful connection
	Serial.print("Connecting to WiFi");
	while (WiFiMulti.run() != WL_CONNECTED) {
		delay(500);
		display.print(".");
		display.display();
		Serial.print(".");

	}

	display.clearDisplay();
	display.setCursor(0, 0);
	display.println("Connected to: ");
	display.println(ssid);
	display.println(WiFi.localIP());
	display.display();

	Serial.print("\nConnected to: ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println("");

	sfdcPEClient.setOAuth2Params(sfdc_client_id, sfdc_secret_id);
	sfdcPEClient.login(sfdc_user, sfdc_password, sfdc_token,false);

	Serial.print("Instance URL:");
	Serial.println(sfdcPEClient.getInstanceURL());

	strcpy(instanceURL, sfdcPEClient.getInstanceURL());

	Serial.print("Token:");
	Serial.println(sfdcPEClient.getToken());

	strcpy(token, sfdcPEClient.getToken());

	SPI.begin();      // Init SPI bus
	mfrc522.PCD_Init(); // Init MFRC522 card

	display.println("Scan client card...");
	display.display();
	Serial.println("Please scan client card...");

}

int createCustomerVisitEvent(char *customerNumber, char *productCode,
bool checkout) {
	int result;
	//StaticJsonBuffer<1024> jsonBuffer;
	String macAddress = WiFi.macAddress();
	CustomerVisit custVisitEvent = CustomerVisit(macAddress,
			String(customerNumber), String(productCode), checkout);

	if ((WiFiMulti.run() == WL_CONNECTED)) {

		result = sfdcPEClient.postEvent("CustomerVisit__e", &custVisitEvent);
		return result;
	} else {
		return -1;
	}

}

void handleKey() {
	static unsigned long last_interrupt_time = 0;
	unsigned long interrupt_time = millis();
	// If interrupts come faster than 200ms, assume it's a bounce and ignore
	if (interrupt_time - last_interrupt_time > 200) {

		switch (state) {
		case STATE_SCAN_PRODUCTS:

			state = STATE_CHECKOUT;
			changeStateFlag = true;
			break;
		case STATE_CHECKOUT:
			state = STATE_SCAN_CLIENT_CARD;

			changeStateFlag = true;
			break;
		default:
			break;
		}

	}
	last_interrupt_time = interrupt_time;

}

unsigned long getID() {
	if (!mfrc522.PICC_ReadCardSerial()) { //Since a PICC placed get Serial and continue
		return -1;
	}
	unsigned long hex_num;
	hex_num = mfrc522.uid.uidByte[0] << 24;
	hex_num += mfrc522.uid.uidByte[1] << 16;
	hex_num += mfrc522.uid.uidByte[2] << 8;
	hex_num += mfrc522.uid.uidByte[3];
	mfrc522.PICC_HaltA(); // Stop reading
	return hex_num;
}

void loop() {
	char productCode[32];
	char newLineChars[3];
	int numCharRead;
	byte last_state;

	// Look for new cards
	if ((state == STATE_SCAN_CLIENT_CARD) && mfrc522.PICC_IsNewCardPresent()) {
		unsigned long uid = getID();
		if (uid != -1) {
			digitalWrite(CARD_BEEP_PIN, LOW);
			delay(100);
			digitalWrite(CARD_BEEP_PIN, HIGH);

			Serial.print("Customer card detected: ");
			Serial.println(uid);

			sprintf(customerNumber, "%lu", uid);
			display.clearDisplay();
			display.setCursor(0, 0);
			display.display();

			display.println("Card number:");
			display.println(customerNumber);
			display.display();

			state = STATE_SCAN_PRODUCTS;

			changeStateFlag = true;

		}
	}

	// listen to serial Rx from Barcode scanner
	if ((state == STATE_SCAN_PRODUCTS) && Serial.available()) {
		numCharRead = Serial.readBytesUntil('\r', productCode, 100);
		productCode[numCharRead] = 0;
		Serial.readBytes(newLineChars, 3);

		/* after switching on scanner sends whitespace, so we need to exclude this by checking if first character is a digit */
		if(isdigit(productCode[0])){

			display.clearDisplay();
			display.setCursor(0, 0);
			display.display();

			display.println("Product code:");
			display.println(productCode);
			display.display();
			Serial.print("Product code: ");
			Serial.println(productCode);

			createCustomerVisitEvent(customerNumber, productCode, false);
		}

	}

	if (changeStateFlag) {

		Serial.print("State:");
		switch (state) {
		case STATE_SCAN_CLIENT_CARD:
			display.println("Scan client card");
			display.display();
			Serial.println("Waiting for client card");
			break;
		case STATE_SCAN_PRODUCTS:
			createCustomerVisitEvent(customerNumber, "", false);
			display.println("Please scan products");
			display.display();
			Serial.println("Scanning products");
			break;
		case STATE_CHECKOUT:
			digitalWrite(CARD_BEEP_PIN, LOW);
			delay(100);
			digitalWrite(CARD_BEEP_PIN, HIGH);
			display.println("Checkout");
			display.println("Scan client card");
			display.display();
			Serial.println("Checkout");
			createCustomerVisitEvent(customerNumber, "", true);
			state = STATE_SCAN_CLIENT_CARD;
			break;
		}

		changeStateFlag = false;
	}

}

