//Libraries
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <FS.h>
#include <SensorToButton.h>

//Constants
const char* ssid = "Oisky Poisky";  //Wifi name
const char* wifipwd = "bananflue";  //Wifi password
const int flowerID = 1;

//Pin definitions
int piezo = D0;
int button = D8;
int buttonled = D5;
int led = D4;

//Pin definition for debounced button. 50 is debounce time.
SensorToButton simpleButton(button, 50);

//Global variables
unsigned long previousTime = 0;
unsigned long interval = 300;
boolean otherAvailable;
boolean selfAvailable = false;
boolean hasNotificationPlayed = false;

//FTP server login details
char* xhost = "172.104.241.160";
char* xusername = "gormo94";
char* xpassword = "h4o%A1g4";
char* xfolder = "";

//file to upload
char* xfilename = "status1.txt";

short FTPresult;

//Function prototype - required if folder is an optional argument
short doFTP(char* , char* , char* , char* , char* = "");

void setup() {
  Serial.begin(115200);

  //Object initialisation
  pinMode(led, OUTPUT);
  pinMode(piezo, OUTPUT);
  pinMode(button, INPUT);
  pinMode(buttonled, OUTPUT);

  digitalWrite(buttonled, LOW); //Default buttonled to low

  wifiSetup();  //Connecting to wifi
  SPIFFS.begin();   //Inititalizing SPIFFS for file management
  getOtherStatus(); //Checks status of other flowers and reacts appropriately. In the prototype case we communicate only with one other flower, instead of 6.
}

void loop() {
  delay(50);
  if (millis() - previousTime > 30000) {      //Makes a HTTPS GET-request to check status of other flowers once every 30s
    getOtherStatus();
    previousTime = millis();
  }
  ownStatus();      //Sends own status to server via FTP-protocol whenever button is pressed
  notification();   //Plays notification if own flower is available and someone else becomes available
}




//---------------------------------------- Methods
   void ownStatus() {
    simpleButton.read();
    if (simpleButton.wasPressed() && !selfAvailable) { // wasPressed() includes automatic debouncing
      digitalWrite(buttonled, HIGH);
      selfAvailable = true;
      Serial.println("Own flower available");
      writeOwnStatusToFile();  //Writes "1" to status1.txt on webserver via FTP, indicating that selfAvailable = true.
    }
    else if (simpleButton.wasPressed() && selfAvailable) {
      digitalWrite(buttonled, LOW);
      selfAvailable = false;
      hasNotificationPlayed = false;
      Serial.println("Own flower unavailable");
      writeOwnStatusToFile(); //Writes "0" to status1.txt on webserver via FTP, indicating that selfAvailable = false.
    }
  }

  void notification() {  //Instructs piezo to play a notification, given certain conditions.
    if (selfAvailable && otherAvailable && (!hasNotificationPlayed)) {
      previousTime = millis();
      while (millis() - previousTime < interval) {
        tone(piezo, 391);
      }
      previousTime = millis();

      while (millis() - previousTime < interval) {
        tone(piezo, 440);
      }
      previousTime = millis();

      while (millis() - previousTime < interval) {
        tone(piezo, 349);
      }
      previousTime = millis();

      while (millis() - previousTime < interval) {
        tone(piezo, 174);
      }
      previousTime = millis();

      while (millis() - previousTime < interval) {
        tone(piezo, 261);
      }
      noTone(piezo);
      hasNotificationPlayed = true;  //Sets flag to to true, so that notification won't play again until either you have pressed on+off, or partner has pressed on+off.
    }
  }

  void wifiSetup() {  //Connects to wifi
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, wifipwd);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("----------------------------------------");
  }

  void getOtherStatus() { //Fetches statuses of other flowers (only 1 in this case, easily scaleable) by way of HTTPS GET-request.
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure); //Webserver is on HTTPS-protocol, not HTTP-protocol, so we need to use BearSSL to manufacture a certificate.
    client->setInsecure(); //Uses insecure file-transfer, to avoid having to use fingerprints. This makes the flower vulnerable to man-in-the-middle attacks, which is not much of a concern given the functionality of the flower.
    HTTPClient https;
    Serial.print("[HTTPS] begin... \n");
    if (https.begin(*client, "https://osborg.no/flower/status2.txt")){
      int httpCode = https.GET();
      if (httpCode > 0) {
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          String payload = https.getString();  //Saves osborg.no/flower/status2.txt content to string "payload".
          if (payload == "1") {
            Serial.println("Flower 2 available");
            otherAvailable = true;
            digitalWrite(led, HIGH);
          }
          else if (payload == "0") {
            Serial.println("Flower 2 unavailable");
            otherAvailable = false;
            digitalWrite(led, LOW);
            hasNotificationPlayed = false;
          }

          else {
            Serial.println("Payload not recognized");
          }
        } else {
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }
      }
      https.end();
    } else {
      Serial.printf("[HTTPS] unable to connect\n");
    }
  }

  void writeOwnStatusToFile() { //Writes own status to file using SPIFFS file management.
    //Opens file
    File f = SPIFFS.open(xfilename, "w+");
    if (!f) {
      Serial.println("Failed to open file");
      Serial.println("Terminating...");
    }
    else {
      Serial.println("File status1.txt exists in SPIFFS");
      //Write data to file
      if (selfAvailable) {
        f.println("1");
      } else {
        f.println("0");
      }
      f.close();
      //Print contents of root folder
      String str = "";
      Dir dir = SPIFFS.openDir("");
      while (dir.next()) {
        str += dir.fileName();
        str += " / ";
        str += dir.fileSize();
        str += "\r\n";
      }
      Serial.print(str);
      Serial.println("-------------------------------------");
    }
    //Attempt FTP upload
    FTPresult = doFTP(xhost, xusername, xpassword, xfilename, xfolder);
    Serial.println("A return code of 226 is success");
    Serial.print("Return code = ");
    Serial.println(FTPresult);
  }

  /*------------------------------------------------------
     Return codes:
        226 - a successful transfer
        400+ - any return code greater than 400 indicates
        an error. These codes are defined at
        https://en.wikipedia.org/wiki/List_of_FTP_server_return_codes
        Exceptions to this are:
        900 - failed to open file on SPIFFS
        910 - failed to connect to server

     Dependencies:
       Libraries - <ESP8266WiFi.h> wifi library
                   <FS.h> SPIFFS library
       Functions - eRcv
    --------------------------------------------------------*/
  short doFTP(char* host, char* uname, char* pwd, char* fileName, char* folder) //Connects and uploads to FTP-server. This method is source from here: https://forum.arduino.cc/index.php?topic=590977.0
  {
    WiFiClient ftpclient;
    WiFiClient ftpdclient;

    const short FTPerrcode = 400; //error codes are > 400
    const byte Bufsize = 128;
    char outBuf[Bufsize];
    short FTPretcode = 0;
    const byte port = 21; //21 is the standard connection port

    File ftx = SPIFFS.open(fileName, "r"); //file to be transmitted
    if (!ftx) {
      Serial.println(F("file open failed"));
      return 900;
    }
    if (ftpclient.connect(host, port)) {
      Serial.println(F("Connected to FTP server"));
    }
    else {
      ftx.close();
      Serial.println(F("Failed to connect to FTP server"));
      return 910;
    }
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;

    /* User - Authentication username
       Send this command to begin the login process. username should be a
       valid username on the system, or "anonymous" to initiate an anonymous login.
    */
    ftpclient.print("USER ");
    ftpclient.println(uname);
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;

    /* PASS - Authentication password
       After sending the USER command, send this command to complete
       the login process. (Note, however, that an ACCT command may have to be
       used on some systems, not needed with synology diskstation)
    */
    ftpclient.print("PASS ");
    ftpclient.println(pwd);
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;

    //CWD - Change the working folder on the FTP server
    if (!(folder == "")) {
      ftpclient.print("CWD ");
      ftpclient.println(folder);
      FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
      if (FTPretcode >= 400) {
        return FTPretcode;
      }
    }

    /* SYST - Returns a word identifying the system, the word "Type:",
       and the default transfer type (as would be set by the
       TYPE command). For example: UNIX Type: L8 - this is what
       the diskstation returns
    */
    ftpclient.println("SYST");
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;

    /* TYPE - sets the transfer mode
       A - ASCII text
       E - EBCDIC text
       I - image (binary data)
       L - local format
       for A & E, second char is:
       N - Non-print (not destined for printing). This is the default if
       second-type-character is omitted
       Telnet format control (<CR>, <FF>, etc.)
       C - ASA Carriage Control
    */
    ftpclient.println("Type I");
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;

    /* PASV - Enter passive mode
       Tells the server to enter "passive mode". In passive mode, the server
       will wait for the client to establish a connection with it rather than
       attempting to connect to a client-specified port. The server will
       respond with the address of the port it is listening on, with a message like:
       227 Entering Passive Mode (a1,a2,a3,a4,p1,p2), e.g. from diskstation
       Entering Passive Mode (192,168,0,5,217,101)
    */
    ftpclient.println("PASV");
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) return FTPretcode;
    /* This is parsing the return from the server
       where a1.a2.a3.a4 is the IP address and p1*256+p2 is the port number.
    */
    char *tStr = strtok(outBuf, "(,"); //chop the output buffer into tokens based on the delimiters
    int array_pasv[6];
    for ( int i = 0; i < 6; i++) { //there are 6 elements in the address to decode
      tStr = strtok(NULL, "(,"); //1st time in loop 1st token, 2nd time 2nd token, etc.
      array_pasv[i] = atoi(tStr); //convert to int, why atoi - because it ignores any non-numeric chars
      //after the number
      if (tStr == NULL) {
        Serial.println(F("Bad PASV Answer"));
      }
    }
    //extract data port number
    unsigned int hiPort, loPort;
    hiPort = array_pasv[4] << 8; //bit shift left by 8
    loPort = array_pasv[5] & 255; //bitwise AND
    Serial.print(F("Data port: "));
    hiPort = hiPort | loPort; //bitwise OR
    Serial.println(hiPort);
    //first instance of dftp
    if (ftpdclient.connect(host, hiPort)) {
      Serial.println(F("Data port connected"));
    }
    else {
      Serial.println(F("Data connection failed"));
      ftpclient.stop();
      ftx.close();
    }

    /* STOR - Begin transmission of a file to the remote site. Must be preceded
       by either a PORT command or a PASV command so the server knows where
       to accept data from
    */
    ftpclient.print("STOR ");
    ftpclient.println(fileName);
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) {
      ftpdclient.stop();
      return FTPretcode;
    }
    Serial.println(F("Writing..."));

    byte clientBuf[64];
    int clientCount = 0;

    while (ftx.available()) {
      clientBuf[clientCount] = ftx.read();
      clientCount++;
      if (clientCount > 63) {
        ftpdclient.write((const uint8_t *)clientBuf, 64);
        clientCount = 0;
      }
    }
    if (clientCount > 0) ftpdclient.write((const uint8_t *)clientBuf, clientCount);
    ftpdclient.stop();
    Serial.println(F("Data disconnected"));
    FTPretcode = eRcv(ftpclient, outBuf, Bufsize);
    if (FTPretcode >= 400) {
      return FTPretcode;
    }

    //End the connection
    ftpclient.println("QUIT");
    ftpclient.stop();
    Serial.println(F("Disconnected from FTP server"));

    ftx.close();
    Serial.println(F("File closed"));
    return FTPretcode;
  } // end function doFTP
  /*------------------------------------------------------
     FUNCTION - eRcv
     Reads the response from an FTP server and stores the
     output in a buffer.Extracts the server return code from
     the buffer.

     Parameters passed:
       aclient - a wifi client connected to FTP server and
       delivering the server response
       outBuf - a buffer to store the server response on
       size - size of the buffer in bytes

     Return codes:
        These are the first three chars in the buffer and are
        defined in
        https://en.wikipedia.org/wiki/List_of_FTP_server_return_codes

     Dependencies:
       Libraries - <ESP8266WiFi.h> wifi library
       Functions - none
    --------------------------------------------------------*/
  short eRcv(WiFiClient aclient, char outBuf[], int size) //Reads FTP-server response. This method is sourced from here: https://forum.arduino.cc/index.php?topic=590977.0
  {
    byte thisByte;
    char index;
    String respStr = "";
    while (!aclient.available()) delay(1);
    index = 0;
    while (aclient.available()) {
      thisByte = aclient.read();
      Serial.write(thisByte);
      if (index < (size - 2)) { //less 2 to leave room for null at end
        outBuf[index] = thisByte;
        index++;
      }
    } //note if return from server is > size it is truncated.
    outBuf[index] = 0; //putting a null because later strtok requires a null-delimited string
    //The first three bytes of outBuf contain the FTP server return code - convert to int.
    for (index = 0; index < 3; index++) {
      respStr += (char)outBuf[index];
    }
    return respStr.toInt();
  } // end function eRcv
