/***************************************
Thanks to - Handiko Gesang - www.github.com/handiko
Handiko Gesang dia guna Dorji module untuk RF, saya tak ada module itu. 
Jadi saya buang koding Dorji, RF datang terus dari radio

D2      - Audio out put MIC (+)
D7      - Relay PTT Controller (pin IN)
D8      - TX pin di GPS
D9      - RX pin di GPS (kadang2 tak sambung mana2 pun jadi)
D13     - LED PTT ON/OFF (+)
GND     - LED PTT ON/OFF (-)
GND     - GND Relay PTT Controller (-)
GND     - GND MIC (-) diperlukan jika menggunakan handy
3.3V    - VCC 1 way Relay PTT Controller  
5.0V    - VCC 2 way Relay PTT Controller 

VCC dan GND untuk GPS Module, ambik di pin ICSP

Pada 10hb Mac, 2023 tambah LED indicator bagi jenis-jenis msg yang akan di TX

*************************************/

#include <math.h>
#include <stdio.h>
#include <SoftwareSerial.h>

// Defines the Square Wave Output Pin
#define OUT_PIN 2               // audio out, asal 12
#define PTT_LED 13              // LED PTT activity 

#define _1200   1
#define _2400   0

#define _FLAG       0x7e
#define _CTRL_ID    0x03
#define _PID        0xf0

// #define _DT_EXP     ','      // asal
#define _DT_EXP     ';'         // Object indentifier
#define _DT_STATUS  '>'         // asal
#define _DT_POS     '!'

#define _GPRMC          1
#define _FIXPOS         2
#define _FIXPOS_STATUS  3
#define _STATUS         4
#define _BEACON         5

// LED Indicator 
#define LED_GPRMC          12     // Biru
#define LED_FIXPOS         11     // Hijau
#define LED_FIXPOS_STATUS  10     // Merah
#define LED_STATUS          4     // Kuning
#define LED_BEACON          3     // Puteh

// Defines the Dorji Control PIN
#define _PTT      7
#define _PD       6
#define _POW      5

bool nada = _2400;

/*
 * SQUARE WAVE SIGNAL GENERATION
 * 
 * baud_adj lets you to adjust or fine tune overall baud rate
 * by simultaneously adjust the 1200 Hz and 2400 Hz tone,
 * so that both tone would scales synchronously.
 * adj_1200 determined the 1200 hz tone adjustment.
 * tc1200 is the half of the 1200 Hz signal periods.
 * 
 *      -------------------------                           -------
 *     |                         |                         |
 *     |                         |                         |
 *     |                         |                         |
 * ----                           -------------------------
 * 
 *     |<------ tc1200 --------->|<------ tc1200 --------->|
 *     
 * adj_2400 determined the 2400 hz tone adjustment.
 * tc2400 is the half of the 2400 Hz signal periods.
 * 
 *      ------------              ------------              -------
 *     |            |            |            |            |
 *     |            |            |            |            |            
 *     |            |            |            |            |
 * ----              ------------              ------------
 * 
 *     |<--tc2400-->|<--tc2400-->|<--tc2400-->|<--tc2400-->|
 *     
 */
const float baud_adj = 0.975;
const float adj_1200 = 1.0 * baud_adj;
const float adj_2400 = 1.0 * baud_adj;
unsigned int tc1200 = (unsigned int)(0.5 * adj_1200 * 1000000.0 / 1200.0);
unsigned int tc2400 = (unsigned int)(0.5 * adj_2400 * 1000000.0 / 2400.0);

/*
 * This strings will be used to generate AFSK signals, over and over again.
 */
char mycall[8] = "9W2KEY";        // sila tukar kepada callsign anda yang sah
char myssid = 2;

char dest[8] = "APZKY1";          //  APZxxx adalah The AX.25 Destination Address bagi Experimental
char dest_beacon[8] = "BEACON";

char digi[8] = "WIDE2";
char digissid = 1;                // asal dia guna WIDE2-2

char comment[128] = "Experiment APRS Arduino(NANO) ..:|GPS station|:..";
char mystatus[128] = "9w2key.hopto.org";

char lati[9];
char lon[10];
int coord_valid;
const char sym_ovl = 'Y';
/****** List symbols at http://www.aprs.org/symbols.html ********/
// const char sym_tab = 'p';      // Partly Cloudy
// const char sym_tab = 'U';      // Matahari cerah
const char sym_tab = '^';         // Kapal Terbang

unsigned int tx_delay = 1000;     // asal 5000
unsigned int str_len = 400;

unsigned int LED_delay = 200;

char bit_stuff = 0;
unsigned short crc=0xffff;

SoftwareSerial gps = SoftwareSerial(8,9);
char rmc[100];
char rmc_stat;

/*
 * 9w2key.hopto.org / 9w2key.blogspot.com
 */
void set_nada_1200(void);
void set_nada_2400(void);
void set_nada(bool nada);

void send_char_NRZI(unsigned char in_byte, bool enBitStuff);
void send_string_len(const char *in_string, int len);

void calc_crc(bool in_bit);
void send_crc(void);

void send_packet(char packet_type);
void send_flag(unsigned char flag_len);
void send_header(char msg_type);
void send_payload(char type);

char rx_gprmc(void);
char parse_gprmc(void);

void set_io(void);
void print_code_version(void);
void print_debug(char type);

/*
 * de 9W2KEY 
 */
void set_nada_1200(void)
{
  digitalWrite(OUT_PIN, true);
  delayMicroseconds(tc1200);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(tc1200);
}

void set_nada_2400(void)
{
  digitalWrite(OUT_PIN, true);
  delayMicroseconds(tc2400);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(tc2400);
  
  digitalWrite(OUT_PIN, true);
  delayMicroseconds(tc2400);
  digitalWrite(OUT_PIN, LOW);
  delayMicroseconds(tc2400);
}

void set_nada(bool nada)
{
  if(nada)
    set_nada_1200();
  else
    set_nada_2400();
}

/*
 * This function will calculate CRC-16 CCITT for the FCS (Frame Check Sequence)
 * as required for the HDLC frame validity check.
 * 
 * Using 0x1021 as polynomial generator. The CRC registers are initialized with
 * 0xFFFF
 */
void calc_crc(bool in_bit)
{
  unsigned short xor_in;
  
  xor_in = crc ^ in_bit;
  crc >>= 1;

  if(xor_in & 0x01)
    crc ^= 0x8408;
}

void send_crc(void)
{
  unsigned char crc_lo = crc ^ 0xff;
  unsigned char crc_hi = (crc >> 8) ^ 0xff;

  send_char_NRZI(crc_lo, true);
  send_char_NRZI(crc_hi, true);
}

void send_header(char msg_type)
{
  char temp;

  /*
   * APRS AX.25 Header 
   * ........................................................
   * |   DEST   |  SOURCE  |   DIGI   | CTRL FLD |    PID   |
   * --------------------------------------------------------
   * |  7 bytes |  7 bytes |  7 bytes |   0x03   |   0xf0   |
   * --------------------------------------------------------
   * 
   * DEST   : 6 byte "callsign" + 1 byte ssid
   * SOURCE : 6 byte your callsign + 1 byte ssid
   * DIGI   : 6 byte "digi callsign" + 1 byte ssid
   * 
   * ALL DEST, SOURCE, & DIGI are left shifted 1 bit, ASCII format.
   * DIGI ssid is left shifted 1 bit + 1
   * 
   * CTRL FLD is 0x03 and not shifted.
   * PID is 0xf0 and not shifted.
   */

  /********* DEST ***********/
  if(msg_type == _BEACON)
  {
    temp = strlen(dest_beacon);
    for(int j=0; j<temp; j++)
      send_char_NRZI(dest_beacon[j] << 1, true);
  }
  else
  {
    temp = strlen(dest);
    for(int j=0; j<temp; j++)
      send_char_NRZI(dest[j] << 1, true);
  }
  if(temp < 6)
  {
    for(int j=0; j<(6 - temp); j++)
      send_char_NRZI(' ' << 1, true);
  }
  send_char_NRZI('0' << 1, true);
  

  
  /********* SOURCE *********/
  temp = strlen(mycall);
  for(int j=0; j<temp; j++)
    send_char_NRZI(mycall[j] << 1, true);
  if(temp < 6)
  {
    for(int j=0; j<(6 - temp); j++)
      send_char_NRZI(' ' << 1, true);
  }
  send_char_NRZI((myssid + '0') << 1, true);

  
  /********* DIGI ***********/
  temp = strlen(digi);
  for(int j=0; j<temp; j++)
    send_char_NRZI(digi[j] << 1, true);
  if(temp < 6)
  {
    for(int j=0; j<(6 - temp); j++)
      send_char_NRZI(' ' << 1, true);
  }
  send_char_NRZI(((digissid + '0') << 1) + 1, true);

  /***** CTRL FLD & PID *****/
  send_char_NRZI(_CTRL_ID, true);
  send_char_NRZI(_PID, true);
}

void send_payload(char type)
{
  /*
   * APRS AX.25 Payloads
   * 
   * TYPE : POSITION
   * ........................................................
   * |DATA TYPE |    LAT   |SYMB. OVL.|    LON   |SYMB. TBL.|
   * --------------------------------------------------------
   * |  1 byte  |  8 bytes |  1 byte  |  9 bytes |  1 byte  |
   * --------------------------------------------------------
   * 
   * DATA TYPE  : !
   * LAT        : ddmm.ssN or ddmm.ssS
   * LON        : dddmm.ssE or dddmm.ssW
   * 
   * 
   * TYPE : STATUS
   * ..................................
   * |DATA TYPE |    STATUS TEXT      |
   * ----------------------------------
   * |  1 byte  |       N bytes       |
   * ----------------------------------
   * 
   * DATA TYPE  : >
   * STATUS TEXT: Free form text
   * 
   * 
   * TYPE : POSITION & STATUS
   * ..............................................................................
   * |DATA TYPE |    LAT   |SYMB. OVL.|    LON   |SYMB. TBL.|    STATUS TEXT      |
   * ------------------------------------------------------------------------------
   * |  1 byte  |  8 bytes |  1 byte  |  9 bytes |  1 byte  |       N bytes       |
   * ------------------------------------------------------------------------------
   * 
   * DATA TYPE  : !
   * LAT        : ddmm.ssN or ddmm.ssS
   * LON        : dddmm.ssE or dddmm.ssW
   * STATUS TEXT: Free form text
   * 
   * 
   * All of the data are sent in the form of ASCII Text, not shifted.
   * 
   */
  if(type == _GPRMC)
  {
    send_char_NRZI('$', true);
    send_string_len(rmc, strlen(rmc)-1);
  }
  else if(type == _FIXPOS)
  {
    send_char_NRZI(_DT_POS, true);
    send_string_len(lati, strlen(lati));
    send_char_NRZI(sym_ovl, true);
    send_string_len(lon, strlen(lon));
    send_char_NRZI(sym_tab, true);
  }
  else if(type == _STATUS)
  {
    send_char_NRZI(_DT_STATUS, true);
    send_string_len(mystatus, strlen(mystatus));
  }
  else if(type == _FIXPOS_STATUS)
  {
    send_char_NRZI(_DT_POS, true);
    send_string_len(lati, strlen(lati));
    send_char_NRZI(sym_ovl, true);
    send_string_len(lon, strlen(lon));
    send_char_NRZI(sym_tab, true);

    send_string_len(comment, strlen(comment));
  }
  else
  {
    send_string_len(mystatus, strlen(mystatus));
  }
}

/*
 * This function will send one byte input and convert it
 * into AFSK signal one bit at a time LSB first.
 * 
 * The encode which used is NRZI (Non Return to Zero, Inverted)
 * bit 1 : transmitted as no change in tone
 * bit 0 : transmitted as change in tone
 */
void send_char_NRZI(unsigned char in_byte, bool enBitStuff)
{
  bool bits;
  
  for(int i = 0; i < 8; i++)
  {
    bits = in_byte & 0x01;

    calc_crc(bits);

    if(bits)
    {
      set_nada(nada);
      bit_stuff++;

      if((enBitStuff) && (bit_stuff == 5))
      {
        nada ^= 1;
        set_nada(nada);
        
        bit_stuff = 0;
      }
    }
    else
    {
      nada ^= 1;
      set_nada(nada);

      bit_stuff = 0;
    }

    in_byte >>= 1;
  }
}

void send_string_len(const char *in_string, int len)
{
  for(int j=0; j<len; j++)
    send_char_NRZI(in_string[j], true);
}

void send_flag(unsigned char flag_len)
{
  for(int j=0; j<flag_len; j++)
    send_char_NRZI(_FLAG, LOW); 
}

/*
 * In this preliminary test, a packet is consists of FLAG(s) and PAYLOAD(s).
 * Standard APRS FLAG is 0x7e character sent over and over again as a packet
 * delimiter. In this example, 100 flags is used the preamble and 3 flags as
 * the postamble.
 */
void send_packet(char packet_type)
{
  print_debug(packet_type);

  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(_PTT, HIGH);
  digitalWrite(PTT_LED, HIGH);

  delay(700);                     // delay untuk tekan PTT dulu kejap, kemudian baru audio tubik

  /*
   * AX25 FRAME
   * 
   * ........................................................
   * |  FLAG(s) |  HEADER  | PAYLOAD  | FCS(CRC) |  FLAG(s) |
   * --------------------------------------------------------
   * |  N bytes | 22 bytes |  N bytes | 2 bytes  |  N bytes |
   * --------------------------------------------------------
   * 
   * FLAG(s)  : 0x7e
   * HEADER   : see header
   * PAYLOAD  : 1 byte data type + N byte info
   * FCS      : 2 bytes calculated from HEADER + PAYLOAD
   */
  
  send_flag(200);
  crc = 0xffff;
  send_header(packet_type);
  send_payload(packet_type);
  send_crc();
  send_flag(3);

  delay(400);                     // delay untuk tunggu kejap, sebelum lepas PTT
  digitalWrite(_PTT, LOW);
  digitalWrite(LED_BUILTIN, 0);
  digitalWrite(PTT_LED, LOW);  
}

/*
 * Function to randomized the value of a variable with defined low and hi limit value.
 * Used to create random AFSK pulse length.
 */
void randomize(unsigned int &var, unsigned int low, unsigned int high)
{
  randomSeed(analogRead(A0));
  var = random(low, high);
}

/*
 * de 9W2KEY
 */
char rx_gprmc(void)
{
  char temp;
  int c=0;

  for(int i=0;i<100;i++)
    rmc[i]=0;

  do
  {
    if(gps.available()>0)
      temp = gps.read(); 
  }
  while(temp!='$');

  do
  {
    if(gps.available()>0)
    {
      temp = gps.read();
      rmc[c] = temp;
      c++;
    }

    if(c==5)
    {
      if(rmc[4]!='C')
      {
        return 0;

        goto esc;
      }
    }
  }
  while((temp!=10)&&(temp!=13));

  c--;

  return c;

  esc:
  ;
}

char parse_gprmc(void)
{
  gps.begin(9600);

  rmc_stat = 0;
  rmc_stat = rx_gprmc();

  gps.flush();
  gps.end();

  if(rmc_stat > 10)
    return rmc_stat;
  else
    return 0;
}

int get_coord(void)
{
  /* latitude */
  for(int i=0;i<7;i++)
  {
    lati[i] = rmc[i+18];
  }

  lati[7]=rmc[29];

  /* Longitude */
  for(int i=0;i<8;i++)
  {
    lon[i] = rmc[i+31];
  }

  lon[8]=rmc[43];

  if(rmc[16]=='A')
    return 1;
  else if(rmc[16]=='V')
    return 0;
  else
    return -1;
}

/*
 * 9w2key.hopto.org / 9w2key.blogspot.com
 */
void set_io(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(OUT_PIN, OUTPUT);
  pinMode(PTT_LED, OUTPUT);

//  pinMode(DRJ_RXD, INPUT);
//  pinMode(DRJ_TXD, OUTPUT);
  pinMode(_PTT, OUTPUT);
  pinMode(_PD, OUTPUT);
  pinMode(_POW, OUTPUT);

  pinMode(LED_GPRMC, OUTPUT);
  pinMode(LED_FIXPOS, OUTPUT);
  pinMode(LED_FIXPOS_STATUS, OUTPUT);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(LED_BEACON, OUTPUT);

  digitalWrite(_PTT, LOW);
  digitalWrite(_PD, HIGH);
  digitalWrite(_POW, LOW);

  Serial.begin(115200);
//  dorji.begin(9600);
}

void print_code_version(void)
{
  Serial.println(" ");
  Serial.print("Sketch:   ");   Serial.println(__FILE__);
  Serial.print("Uploaded: ");   Serial.println(__DATE__);
  Serial.println(" ");
  
  Serial.println("GPRMC APRS Transmitter - Started ! \n");
}

void print_debug(char type)
{
  /*
   * PROTOCOL DEBUG.
   * 
   * Will outputs the transmitted data to the serial monitor
   * in the form of TNC2 string format.
   * 
   * MYCALL-N>APRS,DIGIn-N:<PAYLOAD STRING> <CR><LF>
   */
  Serial.begin(115200);

  /****** MYCALL ********/
  Serial.print(mycall);
  Serial.print('-');
  Serial.print(myssid, DEC);
  Serial.print('>');
  
  /******** DEST ********/
  if(type == _BEACON)
    Serial.print(dest_beacon);
    
  else
    Serial.print(dest);
    Serial.print(',');

    digitalWrite(LED_BEACON, HIGH);
    delay(LED_delay);
    digitalWrite(LED_BEACON, LOW);

  /******** DIGI ********/
  Serial.print(digi);
  Serial.print('-');
  Serial.print(digissid, DEC);
  Serial.print(':');

  /******* PAYLOAD ******/
  if(type == _GPRMC)
  {
    Serial.print('$');
    Serial.print(rmc);

    digitalWrite(LED_GPRMC, HIGH);
    // delay(LED_delay);
    // digitalWrite(LED_GPRMC, LOW);    
  }
  else if(type == _FIXPOS)
  {
    Serial.print(_DT_POS);
    Serial.print(lati);
    Serial.print(sym_ovl);
    Serial.print(lon);
    Serial.print(sym_tab);

    digitalWrite(LED_FIXPOS, HIGH);
    // delay(LED_delay);
    // digitalWrite(LED_FIXPOS, LOW);    
  }
  else if(type == _STATUS)
  {
    Serial.print(_DT_STATUS);
    Serial.print(mystatus);

    digitalWrite(LED_STATUS, HIGH);
    // delay(LED_delay);
    // digitalWrite(LED_STATUS, LOW);
    
  }
  else if(type == _FIXPOS_STATUS)
  {
    Serial.print(_DT_POS);
    Serial.print(lati);
    Serial.print(sym_ovl);
    Serial.print(lon);
    Serial.print(sym_tab);
    Serial.print(comment);

    digitalWrite(LED_FIXPOS_STATUS, HIGH);
    // delay(LED_delay);
    // digitalWrite(LED_FIXPOS_STATUS, LOW);
  }
  else
  {
    Serial.print(mystatus);
  }
  
  Serial.println(' ');

  Serial.flush();
  Serial.end();
}

/*
 * 
 */
void dorji_init(SoftwareSerial &ser)
{
  ser.println("AT+DMOCONNECT");
}

void dorji_reset(SoftwareSerial &ser)
{
  for(char i=0;i<3;i++)
    ser.println("AT+DMOCONNECT");
}

void dorji_setfreq(float txf, float rxf, SoftwareSerial &ser)
{
  ser.print("AT+DMOSETGROUP=0,");
  ser.print(txf, 4);
  ser.print(',');
  ser.print(rxf, 4);
  ser.println(",0000,0,0000");
}

void dorji_readback(SoftwareSerial &ser)
{
  String d;
  
  while(ser.available() < 1);
  if(ser.available() > 0)
  {
    d = ser.readString();
    Serial.print(d);
  }
}

void dorji_close(SoftwareSerial &ser)
{
  ser.end();
}

/*
 * 9w2key.blogspot.com / 9w2key.hopto.org
 */
void setup()
{
  set_io();
  print_code_version();

  delay(250);
  
//  dorji_reset(dorji);
//  dorji_readback(dorji);
//  delay(1000);
//  dorji_setfreq(144.390, 144.390, dorji);
//  dorji_readback(dorji);

  Serial.println(' ');

//  dorji_close(dorji);
  gps.begin(9600);

  randomSeed(analogRead(A0));
}

void loop()
{

/***********************
Nak tutup LED Msg Type
************************/
digitalWrite(LED_BEACON, LOW);
digitalWrite(LED_GPRMC, LOW);
digitalWrite(LED_FIXPOS, LOW);
digitalWrite(LED_STATUS, LOW);
digitalWrite(LED_FIXPOS_STATUS, LOW);
  
   parse_gprmc();
  coord_valid = get_coord();
  
  if(rmc_stat > 10)
  {
    // send_packet(random(1,4), random(1,3));
    if(coord_valid > 0)
      send_packet(random(5));
    else
      send_packet(_BEACON);
  }
 
  delay(tx_delay);                              // setting tx_delay asal 5000 (5 saat)
  // randomize(tx_delay, 14000, 16000);         // asal 14000, 16000 (14 saat hingga 16 saat)
  // randomize(tx_delay, 2000, 3000);           // setting untuk test system
  // randomize(tx_delay, 18000, 30000);         // setting nak testing 3 hingga 5 minit
  // randomize(tx_delay, 6000, 10000);          // setting nak TX 6 hingga 10 saat 
  randomize(tx_delay, 12000, 20000);            // setting nak TX 12 hingga 20 saat, kereta guna hok ni sekarang

}