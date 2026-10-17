#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include <WiFi.h>
#include "hardware/adc.h"
#include <hardware/timer.h>
#include <hardware/irq.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#define GETBIT(x, n) (((x) >> (n)) & 1)       //GETBIT(keybuffer, 19)
#define WIFI_SSID     "network_name"
#define WIFI_PASSWORD "network_password"
#define DEST_IP       "192.168.1.158"
#define DEST_PORT     8080
#define ALARM_NUM     0
#define ALARM_IRQ  TIMER_IRQ_0
#define PERIOD_US  2000   // 2 ms
#define PIN_A  21
#define PIN_B  20
#define E_BUTT  22
#define BUTTONS 21
#define KEYPAD_SIZE 16
#define MENUPAD_SIZE 5
#define REFERENCE_NOTE 48
#define NUM_TASKR (sizeof(taskr) / sizeof(taskr[0])) 
//## Super Binario 2 #####
#define SUSTAIN     0x00008000
#define MENULOCK    0x00010000
#define CHANELLOCK  0x00020000
#define MENU        0x001F0000   
#define KEYS        0x0000FFFF
#define PANIC       0x00400000

const int key_pin[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8,9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19,22 }; 
uint8_t note[KEYPAD_SIZE] = {};
uint8_t octave=4,modulation = 0,modulation2 = 0,notedistance = 1,channelselect = 0,velocity = 99,x_val = 63,y_val = 63,adc_cnt = 0,x_cnt = 0,y_cnt = 0,vol = 100;
uint16_t sensorValue = 0; 
uint32_t binario=0,currentMicros,bptime = 100000,adc_sum = 0,x_sum = 0,y_sum = 0;
//buttons
volatile uint32_t keybuffer=0xFFFFFFFF;
volatile uint16_t keys=KEYS;
volatile uint8_t  menu=0xFF;
//udp stuff
struct udp_pcb *pcb;
ip_addr_t dest;
uint32_t count = 0;
//encoder stuff
uint8_t prev = 3;  // last step number
uint8_t dir  = 0;  // 0=none, 1=fwd, 3=bwd
uint8_t cnt  = 0;  // consecutive same-dir count
volatile int32_t delta = 0;
const uint8_t cc_table[12] = { 72,71,14,95,93,9,80,94,97,95,5,96 };
int32_t cc_vals[12] = {0};
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIusb);

static void alarm_irq(void) {
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + PERIOD_US;
  uint8_t cur = (gpio_get(PIN_A) << 1) | gpio_get(PIN_B);
  uint8_t idx = (cur << 2) | prev;
  //printBin4(idx);
  switch (idx) {
    // ── forward ── 11→10→00→01→11 ──
  case 11: case 2: case 4: case 13:
    if (dir != 1) { dir = 1; cnt = 0; }
    cnt++;
    if (cnt == 4) {delta++;cnt = 0;}
    break;

  // ── backward ──
  case 7: case 1: case 8: case 14:
    if (dir != 3) { dir = 3; cnt = 0; }
    cnt++;
    if (cnt == 4) {delta--;cnt = 0;}
    break;
  // ── invalid (two bits flipped) → reset ──
  case 3: case 6: case 9: case 12:
    cnt = 0;
    break;

  // ── no change (idle) → do nothing ──
  default:
    break;
  }
  prev = cur;
}
void analread_task() {
  adc_select_input(0);
  x_sum += adc_read();
  x_cnt++; 
  adc_select_input(1);
  y_sum += adc_read();
  y_cnt++; 
  adc_select_input(2);
  adc_sum += adc_read();
  adc_cnt++; 
}
void collect(){
  // Joystick X value
  if (x_cnt > 0) {
    sensorValue = x_sum / x_cnt;
    sensorValue = sensorValue & ~3;
    x_cnt = 0;
  }
  x_sum = 0;  
  if(sensorValue >=2100){
    sensorValue = constrain(sensorValue, 2100, 4090);
    sensorValue = map(sensorValue, 4090,2100, 0, 62);   
  }else if(sensorValue <=2040){
    sensorValue = constrain(sensorValue, 50, 2040);
    sensorValue = map(sensorValue, 2040,50, 64, 127);
  }else{sensorValue = 63;}
  if(x_val!=sensorValue){
    x_val=sensorValue;
    shoutout(0xB0 | channelselect, 1, x_val);
    if(tud_ready()){MIDIusb.sendControlChange(1,x_val,channelselect+1);}
  }
  // Joystick Y value
  if (y_cnt > 0) {
    sensorValue = y_sum / y_cnt;
    sensorValue = sensorValue & ~3;
    y_cnt = 0;
  }
  y_sum = 0; 
  if(sensorValue >=2080){
    sensorValue = constrain(sensorValue, 2080, 4090);
    sensorValue = map(sensorValue, 2080,4090, 64, 127);
  }else if(sensorValue <=2020){
    sensorValue = constrain(sensorValue, 60, 2020);
    sensorValue = map(sensorValue, 60,2020, 0, 62);

  }else{sensorValue = 63;}
  if(y_val!=sensorValue){
    y_val=sensorValue;
    shoutout(0xB0 | channelselect, 2, y_val);
    if(tud_ready()){MIDIusb.sendControlChange(2,y_val,channelselect+1);}
  }
  // pot check
  if (adc_cnt > 0) {
    sensorValue = adc_sum / adc_cnt;
    sensorValue = sensorValue & ~3;
    adc_cnt = 0;
  }
  adc_sum = 0;
  sensorValue = constrain(sensorValue, 30, 4088);
  velocity = map(sensorValue, 30,4088, 0, 127);
} 
void encoder_check() {
    int32_t increment = delta;
    delta = 0;
    if (!increment) return;

    uint16_t latch = binario & KEYS;  // ← latched key, not "currently held"

    if (latch) {
        int idx = __builtin_ctz(latch);
        if (idx < 4) {
            switch (idx) {
                case 0: bptime = constrain(bptime + increment * 10000, 0, 10000000); break;
                case 1: notedistance = constrain(notedistance + increment, 0, 9); setNotes(octave * 12); break;
                case 2: octave = constrain(octave + increment, 0, 9); setNotes(octave * 12); break;
                case 3: /* 4th */ break;
            }
            return;
        }
        int ci = idx - 4;
        cc_vals[ci] = constrain(cc_vals[ci] + increment, 0, 127);
        shoutout(0xB0 | channelselect, cc_table[ci], cc_vals[ci]);
        if (tud_ready()) MIDIusb.sendControlChange(cc_table[ci], cc_vals[ci], channelselect + 1);
    } else {
        vol = constrain(vol + increment, 0, 127);
        shoutout(0xB0 | channelselect, 7, vol);
        if (tud_ready()) MIDIusb.sendControlChange(7, vol, channelselect + 1);
    }
}   
//shoutout(0x90 | channelselect, note, velocity);   // Note On
//shoutout(0x80 | channelselect, note, 0);          // Note Off
//shoutout(0xB0 | channelselect, 63, 1);            // CC 63 (Brake) value 1
void shoutout(uint8_t status, uint8_t a, uint8_t b){
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 3, PBUF_RAM);
  uint8_t *d = (uint8_t *)p->payload;
    d[0] = status;   
    d[1] = a;
    d[2] = b;
  udp_sendto(pcb, p, &dest, DEST_PORT);
  pbuf_free(p);
  /*struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 3, PBUF_RAM);
    memcpy(p->payload, bytes, 3);
    udp_sendto(pcb, p, &dest, DEST_PORT);
    pbuf_free(p);*/
}
void printer(){
  //Serial.println(encoderDelta);
  printBin8(menu);
  Serial.print(" ");
  printBin16(keys);
  Serial.println("");
}
void setNotes(uint8_t ref){
  note[0] = ref;
  if(tud_ready()){
    Serial.print(note[0]);
    Serial.print("  ");
  }
  for (int r = 1; r < KEYPAD_SIZE; r++) {
    note[r] = note[r - 1] + notedistance;
    if(tud_ready()){
      Serial.print(note[r]);
      Serial.print("  ");
    }
  }
  if(tud_ready()){Serial.println("  ");}
}
void silence(){  
  if (tud_ready()) {Serial.println("panic pressed: ");}                  
  for(int i=1;i<=16;i++){
    shoutout(0xB0 | (i-1), 127, 0);
    shoutout(0xB0 | (i-1), 122, 0);
    if (tud_ready()) {     
        MIDIusb.sendControlChange(127,0,i);
        MIDIusb.sendControlChange(122,0,i);
    } 
  }
  if (tud_ready()) {Serial.println("panic finished: ");}
  binario|= (0<<15);
}
void printBin16(uint16_t val) {
  for (int i = 15; i >= 0; i--)Serial.print(bitRead(val, i));
} 
void printBin8(uint8_t v) {
  for (int i = 7; i >= 0; i--)Serial.print((v >> i) & 1);
}
void printBin4(uint8_t v) {
  for (int i = 3; i >= 0; i--)Serial.print((v >> i) & 1);
}
void key_check() {
  uint32_t cur = keybuffer & KEYS;
  if (cur == keys) return;        
  switch (binario & (CHANELLOCK | MENULOCK)) {
    case CHANELLOCK:
      for (int d = 0; d < 16; d++) {
        if (GETBIT(cur, d) == 0) {
          channelselect = d;
          printer();
          if (tud_ready()) {
            Serial.print("ch: ");
            Serial.println(channelselect + 1);
          }
          break;
        }
      }
    break;
    case MENULOCK:          
      // Sustain
      if(GETBIT(keybuffer, 3) != GETBIT(keys, 3) && ((binario &MENULOCK)!=0)){
        if(GETBIT(keybuffer, 3) == 0){
          binario^= SUSTAIN;
          if (GETBIT(binario, 15)==1){
            shoutout(0xB0 | channelselect, 64, 120);
            if(tud_ready()){MIDIusb.sendControlChange(64,120,channelselect+1);Serial.print("SUSTAIN: ");Serial.println(GETBIT(binario, 15));}
          }else{
            shoutout(0xB0 | channelselect, 64, 0);
            if(tud_ready()){MIDIusb.sendControlChange(64,0,channelselect+1);Serial.print("SUSTAIN: ");Serial.println(GETBIT(binario, 15));}
          }          
        }else{if (tud_ready()) {Serial.println("SUSTAIN released: ");}}
        keys = (keys & ~(1<<3)) | (keybuffer & (1<<3));   
      }
      for (int i = 0; i < 16; i++) {
            if (GETBIT(keys, i) != GETBIT(cur, i) && GETBIT(cur, i) == 0) {
                binario = (binario & ~KEYS) | (1 << i);
                break;
            }
      }
    break;
    default:  // NORMAL (0)
      for (int i = 0; i < 16; i++) {
        if (GETBIT(keys, i) != GETBIT(cur, i)) {
          if (GETBIT(cur, i) == 0) {
            shoutout(0x90 | channelselect, note[i], velocity);
            if (tud_ready()) MIDIusb.sendNoteOn(note[i], velocity, channelselect + 1);
          } else {
              shoutout(0x80 | channelselect, note[i], 0);
              if (tud_ready()) MIDIusb.sendNoteOff(note[i], 0, channelselect + 1);
          }
        }
      }
      break;
    }
    keys = cur;
}   
void menu_check(){
  // Panic
  if(GETBIT(keybuffer, 22) != GETBIT(menu, 6)){
    if(((keybuffer&PANIC)>>22) == 0){
      menu&= ~(1<<6);
      silence();
    }else{menu|= (1<<6);} 
    printer();   
  }
  // Menu button pressed    
  if(GETBIT(keybuffer, 19) != GETBIT(menu, 3)){
    if(GETBIT(keybuffer, 19) == 0){
      menu&= ~(1<<3);
      binario|= MENULOCK;
      if (tud_ready()) {Serial.println("Menu pressed: ");}
    }else{
      binario &= ~MENULOCK;
      if (tud_ready()) {Serial.println("Menu released: ");}
      menu|= (1<<3);
    }
  }
  // Chanel pressed 
  if(GETBIT(keybuffer, 18) != GETBIT(menu, 2)){
    if(GETBIT(keybuffer, 18) == 0){
      menu&= ~(1<<2);
      binario|= CHANELLOCK;
      if (tud_ready()) {Serial.println("Chanel pressed: ");}
    }else{
      binario &= ~CHANELLOCK;
      if (tud_ready()) {Serial.println("Chanel released: ");}
      menu|= (1<<2);
    }
  }
}  
void pin_check(){
  static uint32_t gpio_state=1;
  static uint32_t old_gpio_state=1;
  static uint32_t oldest_gpio_state=1;
  oldest_gpio_state=old_gpio_state;
  old_gpio_state=gpio_state;
  gpio_state=sio_hw->gpio_in;
  if((gpio_state == old_gpio_state) && (gpio_state != keybuffer) && (old_gpio_state==oldest_gpio_state)){
    keybuffer=gpio_state;
  } 
}
struct Task {
    void (*fn)(void); 
    uint32_t period_us;
    uint32_t last_run;
};
//# task-call #  period # delay/phaseshift    
Task taskr[] = {
  { pin_check,    333,    0   },
  { key_check,    1000,   50  },      
  { menu_check,   3000,   122 },
  { analread_task,1000,   511 }, 
  { collect,      20000,  833 },
  { encoder_check,25000,  1603},
  //{ printer,  500000,   1122 },   
};
void setup() {
  TinyUSBDevice.setManufacturerDescriptor("MusicBox Productions");
  TinyUSBDevice.setProductDescriptor("Midiboy2");
  MIDIusb.begin(MIDI_CHANNEL_OMNI);  // Initiate MIDI communications, listen to all channels
  MIDIusb.turnThruOff();             // turn off echo
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  //delay(2000);
  pcb = udp_new();
  ip4addr_aton(DEST_IP, &dest);
  pinMode(PIN_LED, OUTPUT);
  adc_init();
  adc_gpio_init(26);
  adc_gpio_init(27);
  adc_gpio_init(28);
  setNotes(REFERENCE_NOTE); 
  for (int z = 0; z < 20; z++) {
    pinMode(z, INPUT_PULLUP);
  } 
  gpio_init(PIN_A);
  gpio_init(PIN_B);
  gpio_pull_up(PIN_A);
  gpio_pull_up(PIN_B);
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  irq_set_enabled(ALARM_IRQ, true);
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + PERIOD_US;
}

void loop() {
  currentMicros = micros(); 
  for (int i = 0; i < NUM_TASKR; i++) {
    if (taskr[i].period_us == 0 || currentMicros - taskr[i].last_run >= taskr[i].period_us) {
      taskr[i].fn();
      taskr[i].last_run = currentMicros;
    }
  } 
}
