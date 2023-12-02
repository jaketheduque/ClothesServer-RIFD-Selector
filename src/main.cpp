#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AiEsp32RotaryEncoder.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/*Using Hardware SPI of Arduino */
/*MOSI (11), MISO (12) and SCK (13) are fixed */
/*You can configure SS and RST Pins*/
#define SS_PIN 4  /* Slave Select Pin */
#define RST_PIN 2 /* Reset Pin */

/* Create an instance of MFRC522 */
MFRC522 mfrc522(SS_PIN, RST_PIN);
/* Create an instance of MIFARE_Key */
MFRC522::MIFARE_Key key;

/* Set the block to which we want to write data */
/* Be aware of Sector Trailer Blocks */
int blockNum = 2;
/* Create an array of 16 Bytes and fill it with data */
/* This is the actual data which is going to be written into the card */
byte blockData[16];

/* Create another array to read data from Block */
/* Legthn of buffer should be 2 Bytes more than the size of Block (16 Bytes) */
byte bufferLen = 18;
byte readBlockData[18];

String currentCardUID;
String previousCardUID;

String clothesID;

#define CLK 34
#define DT 35
#define SW 32
int counter = 0;
int currentStateCLK;
int lastStateCLK;
String currentDir = "";
unsigned long lastButtonPress = 0;

bool writingMode = false;

/* 1306 Display Initialization */
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);

/* HTTP Variables */
const char *ssid = "";
const char *password = "";

String serverIP = "";

MFRC522::StatusCode status;

void WriteDataToBlock(int blockNum, byte blockData[])
{
  /* Authenticating the desired data block for write access using Key A */
  status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));
  if (status != MFRC522::STATUS_OK)
  {
    Serial.print("Authentication failed for Write: ");
    Serial.println(mfrc522.GetStatusCodeName((MFRC522::StatusCode)status));
    return;
  }
  else
  {
    Serial.println("Authentication success");
  }

  /* Write data to the block */
  status = mfrc522.MIFARE_Write(blockNum, blockData, 16);
  if (status != MFRC522::STATUS_OK)
  {
    Serial.print("Writing to Block failed: ");
    Serial.println(mfrc522.GetStatusCodeName((MFRC522::StatusCode)status));
    return;
  }
  else
  {
    Serial.println("Data was written into Block successfully");
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void ReadDataFromBlock(int blockNum, byte readBlockData[])
{
  /* Authenticating the desired data block for Read access using Key A */
  byte status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, blockNum, &key, &(mfrc522.uid));

  if (status != MFRC522::STATUS_OK)
  {
    Serial.print("Authentication failed for Read: ");
    Serial.println(mfrc522.GetStatusCodeName((MFRC522::StatusCode)status));
    return;
  }

  /* Reading data from the Block */
  status = mfrc522.MIFARE_Read(blockNum, readBlockData, &bufferLen);
  if (status != MFRC522::STATUS_OK)
  {
    Serial.print("Reading failed: ");
    Serial.println(mfrc522.GetStatusCodeName((MFRC522::StatusCode)status));
    return;
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

void setup()
{
  // Set encoder pins as inputs
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);

  // Read the initial state of CLK
  lastStateCLK = digitalRead(CLK);

  /* Prepare the ksy for authentication */
  /* All keys are set to FFFFFFFFFFFFh at chip delivery from the factory */
  for (byte i = 0; i < 6; i++)
  {
    key.keyByte[i] = 0xFF;
  }

  /* Initialize serial communications with the PC */
  Serial.begin(9600);

  /* Initialize SPI bus */
  SPI.begin();

  /* Initialize MFRC522 Module */
  mfrc522.PCD_Init();
  Serial.println("Scan a MIFARE 1K Tag to write data...");

  /* 1306 Display Setup */
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setFont(NULL);
  display.setCursor(6, 10);
  display.setTextWrap(0);
  display.setCursor(13, 10);
  display.println("Scan RFID Card to");
  display.setTextWrap(0);
  display.setCursor(28, 18);
  display.println("get started!");
  display.display();

  /* WiFi Client Setup */
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected to network with IP: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  /*
   * Rotary encoder section
   */

  // Read the current state of CLK
  currentStateCLK = digitalRead(CLK);

  // If last and current state of CLK are different, then pulse occurred
  // React to only 1 state change to avoid double count
  if (currentStateCLK != lastStateCLK && currentStateCLK == 1 && writingMode == true)
  {

    // If the DT state is different than the CLK state then
    // the encoder is rotating CCW so decrement
    if (digitalRead(DT) != currentStateCLK)
    {
      counter--;
    }
    else
    {
      // Encoder is rotating CW so increment
      counter++;
    }

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setFont(NULL);
    display.setCursor(5, 6);
    display.println("ID to write:");
    display.setCursor(6, 17);
    display.print(counter);
    display.display();
  }

  // Remember last CLK state
  lastStateCLK = currentStateCLK;

  // Read the button state
  int btnState = digitalRead(SW);

  // If we detect LOW signal, button is pressed
  if (btnState == LOW)
  {
    // if 50ms have passed since last LOW pulse, it means that the
    // button has been pressed, released and pressed again
    if (millis() - lastButtonPress > 50)
    {
      writingMode = !writingMode;
      Serial.print("Writing Mode: ");
      Serial.println(writingMode);

      if (writingMode)
      {
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setFont(NULL);
        display.setCursor(5, 6);
        display.println("ID to write:");
        display.setCursor(6, 17);
        display.print(counter);
        display.display();
      }
      else
      {
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setFont(NULL);
        display.setCursor(6, 10);
        display.setTextWrap(0);
        display.setCursor(13, 10);
        display.println("Scan RFID Card to");
        display.setTextWrap(0);
        display.setCursor(28, 18);
        display.println("get started!");
        display.display();
      }
    }

    // Remember last button press event
    lastButtonPress = millis();
  }

  /*
   * RFID Section
   */

  // Reset loop if no new RFID card is present
  if (!mfrc522.PICC_IsNewCardPresent())
  {
    return;
  }

  // Selects one of the RFID cards
  if (!mfrc522.PICC_ReadCardSerial())
  {
    return;
  }

  // Gets card UID
  for (byte i = 0; i < mfrc522.uid.size; i++)
  {
    currentCardUID += mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ";
    currentCardUID += String(mfrc522.uid.uidByte[i], HEX);
  }

  // Checks if the card was previously scanned
  if (currentCardUID == previousCardUID)
  {
    currentCardUID = "";
    return;
  }

  Serial.print("\n");
  Serial.println("New Card Detected!");

  // Print out card UID
  Serial.println("Card UID:" + currentCardUID);

  // If in writing mode
  if (writingMode)
  {
    char number[16];
    sprintf(number, "%d", counter);
    String data = String(number);
    data.getBytes(blockData, 16);
    WriteDataToBlock(blockNum, blockData);

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    display.setFont(NULL);
    display.setCursor(0, 15);
    display.setTextWrap(0);
    display.setCursor(37, 15);
    display.println("ID Saved!");
    display.display();
  }
  else
  { // Else if in send mode
    // Reads data from RFID block
    ReadDataFromBlock(blockNum, readBlockData);

    // Print the data read from block
    Serial.print("Data: ");
    for (int j = 0; j < 5; j++)
    {
      Serial.write(readBlockData[j]);
    }
    Serial.print("\n");

    // Parses tag response
    clothesID = String((char *)readBlockData);

    // Checks if connected to WiFi network
    if (WiFi.status() == WL_CONNECTED)
    {
      WiFiClient client;
      HTTPClient http;

      // Updates display
      display.clearDisplay();
      display.setTextColor(WHITE);
      display.setTextSize(1);
      display.setFont(NULL);
      display.setCursor(5, 12);
      display.setTextWrap(0);
      display.setCursor(10, 12);
      display.println("Sending request...");
      display.display();

      // Sends web request
      String requestPath = serverIP + "/getclothes?id=" + clothesID;
      http.begin(client, requestPath.c_str());
      int httpResponseCode = http.GET();

      // Checks response
      if (httpResponseCode > 0)
      {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
        String payload = http.getString();
        Serial.println(payload);

        // Parses JSON return
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        JsonObject root_0 = doc[0];
        const char *clothesName = root_0["name"];

        // Prints recieved clothes name on display
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setFont(NULL);
        display.setCursor(5, 6);
        display.println("ID Sent!");
        display.setCursor(6, 17);
        display.print("Name: ");
        display.print(clothesName);
        display.display();
      }
      else
      {
        Serial.print("Error Sending Request: ");
        Serial.println(httpResponseCode);

        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setTextSize(1);
        display.setFont(NULL);
        display.setCursor(5, 12);
        display.setTextWrap(0);
        display.setCursor(31, 12);
        display.println("HTTP Error!");
        display.display();
      }

      // Free WiFi resources
      http.end();

      previousCardUID = currentCardUID;
      currentCardUID = "";
    }
  }
}
