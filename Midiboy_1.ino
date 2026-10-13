#include <Adafruit_TinyUSB.h>
#include <MIDI.h>
#include "hardware/adc.h"
#include <hardware/timer.h>
#include <hardware/irq.h>
#define ALARM_NUM   0
#define ALARM_IRQ   TIMER_IRQ_0
#define PERIOD_US   2000   // 2 ms between encoder_check interrupts
#define PIN_A  21
#define PIN_B  20
#define E_BUTT  22
#define BUTTONS 21
#define KEYPAD_SIZE 16
#define MENUPAD_SIZE 5
#define REFERENCE_NOTE 48
#define NUM_TASKR (sizeof(taskr) / sizeof(taskr[0])) 
#define SUSTAIN 0b1000000000000000
#define BLOCKED 0b0000000000000001
int16_t pitchbend = 0;
uint8_t modulation = 0;
uint8_t modulation2 = 0;
uint8_t octave=4;
uint16_t binario=0; 
uint8_t note[KEYPAD_SIZE] = {};
uint32_t lastpressed[BUTTONS] = {};
bool noteactive[BUTTONS] = {};
uint32_t btimer[BUTTONS] = {};
uint32_t ftimer[18] = {};
uint32_t lastcalled[18] = {};
uint8_t flagh[18] = {};
uint8_t notedistance = 1;
uint8_t channelselect = 0;
uint8_t blocked = 0;
uint8_t velocity = 99;
uint8_t x_val = 63;
uint8_t y_val = 63;
uint32_t bptime = 100000;
uint32_t currentMicros,lastFire;
uint32_t adc_sum = 0,x_sum = 0,y_sum = 0;
uint8_t  adc_cnt = 0,x_cnt = 0,y_cnt = 0;
uint16_t sensorValue = 0; 
uint8_t vol = 100;
Adafruit_USBD_MIDI usb_midi;
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDIusb);
const int key_pin[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8,9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19,22 };  // 8,9 for RX/TX uart
int currentValue = 0;
bool fineTuneMode = true;
uint8_t prev = 3;  // last step number
uint8_t dir  = 0;  // 0=none, 1=fwd, 3=bwd
uint8_t cnt  = 0;  // consecutive same-dir count
volatile int32_t delta = 0;
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

  // ── backward ──11/01/00/10/11
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
    MIDIusb.sendControlChange(1,x_val,channelselect+1);
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
    MIDIusb.sendControlChange(2,y_val,channelselect+1);
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
void encoder_check(){
  int32_t increment = delta;
  delta = 0;
  vol = constrain(vol + increment, 0, 127);
  if (increment) {
  //The "safe to use" undefined CCs (3, 9,10, 14–15, 20–31,74,79, 85–87, 89–90, 102–119) 
    if((digitalRead(key_pin[19])==0)){
      if(digitalRead(key_pin[8])==0){
        octave = constrain(octave + increment, 0, 9);
        setNotes(octave*12);
      }else if(digitalRead(key_pin[4])==0){
        notedistance = constrain(notedistance + increment, 0, 9);
        setNotes(octave*12);
      }else if(digitalRead(key_pin[0])==0){
        bptime= constrain(bptime + (increment*10000), 0, 10000000);
        Serial.println(bptime);
      }else if(digitalRead(key_pin[13])==0){
        MIDIusb.sendControlChange(73, vol, channelselect+1);
      }else if(digitalRead(key_pin[9])==0){
        MIDIusb.sendControlChange(80, vol, channelselect+1);
      }else if(digitalRead(key_pin[5])==0){
        MIDIusb.sendControlChange(95, vol, channelselect+1);
      }else if(digitalRead(key_pin[1])==0){
        MIDIusb.sendControlChange(72, vol, channelselect+1);
      }else if(digitalRead(key_pin[14])==0){
        MIDIusb.sendControlChange(5, vol, channelselect+1);
      }else if(digitalRead(key_pin[10])==0){
        MIDIusb.sendControlChange(94, vol, channelselect+1);
      }else if(digitalRead(key_pin[6])==0){
        MIDIusb.sendControlChange(93, vol, channelselect+1);
      }else if(digitalRead(key_pin[2])==0){
        MIDIusb.sendControlChange(71, vol, channelselect+1);
      }else if(digitalRead(key_pin[15])==0){
        MIDIusb.sendControlChange(96, vol, channelselect+1);
      }else if(digitalRead(key_pin[11])==0){
        MIDIusb.sendControlChange(97, vol, channelselect+1);
      }else if(digitalRead(key_pin[7])==0){
        MIDIusb.sendControlChange(9, vol, channelselect+1);
      }else if(digitalRead(key_pin[3])==0){
        MIDIusb.sendControlChange(14, vol, channelselect+1);
      }
    }else{MIDIusb.sendControlChange(7, vol, channelselect+1);}
  }
}
void setNotes(uint8_t ref){
  note[0] = ref;
  Serial.print(note[0]);
  Serial.print("  ");
  for (int r = 1; r < KEYPAD_SIZE; r++) {
    note[r] = note[r - 1] + notedistance;
    Serial.print(note[r]);
    Serial.print("  ");
  }
  Serial.println("  ");
}
void silence(){
    for(int i=1;i<=16;i++){
      MIDIusb.sendControlChange(123,127,i);
      MIDIusb.sendControlChange(121,127,i);
      MIDIusb.sendControlChange(64,20,i);
    } 
}
void printBin16(uint16_t val) {
  for (int i = 15; i >= 0; i--)
    Serial.print(bitRead(val, i));
  Serial.println();
} 
void printBin8(uint8_t v) {
  for (int i = 7; i >= 0; i--)
    Serial.print((v >> i) & 1);
  Serial.println();
}
void printBin4(uint8_t v) {
  for (int i = 3; i >= 0; i--)
    Serial.print((v >> i) & 1);
  Serial.println();
}
void key_check(){
  if(!(binario & BLOCKED)){
    for (int i = 0; i < KEYPAD_SIZE; i++) {
      uint8_t kp=digitalRead(key_pin[i]);
      if ((kp==0) && (noteactive[i] == 0)&& (currentMicros >= (btimer[i]+1000))) {
        lastpressed[i] = currentMicros;
        MIDIusb.sendNoteOn(note[i], velocity, channelselect+1);
        noteactive[i] = 1;
        btimer[i] = lastpressed[i] + bptime;
        Serial.print("key pressed: ");
        Serial.println(note[i]);
      }  
      if ((noteactive[i] == 1) && (currentMicros >= btimer[i]) && (kp==1)) {
        MIDIusb.sendNoteOff(note[i], 0, channelselect+1);
        btimer[i] = currentMicros;
        noteactive[i] = 0;
      } 
    }
  }
}
void menu_check(){
// Panic
  if((digitalRead(key_pin[20])==0)&&(noteactive[20]==0)&& (currentMicros >= (btimer[20]+1000))){
    silence();
    lastpressed[20] = currentMicros;
    noteactive[20] = 1;
    btimer[20] = lastpressed[20] + bptime;
    Serial.println("panic pressed: ");
    binario|= (0<<15);
    //printBin16(binario);
  }
  if((noteactive[20]==1)&& (currentMicros >= btimer[20])&&(digitalRead(key_pin[20])==1)){
    noteactive[20] = 0;
    btimer[20] = currentMicros;
    Serial.println("panic released: ");
  } 
// Menu button pressed
  if((digitalRead(key_pin[19])==0)&&(flagh[16]==0)&& (currentMicros >= (ftimer[16]+1000))){
    lastcalled[16] = currentMicros;
    ftimer[16] = lastcalled[16] + bptime;
    binario|= BLOCKED;
    flagh[16] = 1;
    //printBin16(binario);
    Serial.println("Menu pressed: ");
  }
  if((flagh[16]==1)&& (currentMicros >= ftimer[16])&&(digitalRead(key_pin[19])==1)){
    flagh[16] = 0;  
    binario &= ~BLOCKED;
    ftimer[16] = currentMicros;
    printBin16(binario);
    Serial.println("Menu released: ");
    //printBin(binario);
  }
// Sustain
  if((digitalRead(key_pin[12])==0)&&(flagh[16]==1)&&(flagh[15]==0)&&(currentMicros >= (ftimer[15]+1000))){
      lastcalled[15] = currentMicros;
      flagh[15] = 1;
      ftimer[15] = lastcalled[15] + bptime;
      Serial.println("Sustain pressed: ");
      binario^= SUSTAIN;
      if (binario&SUSTAIN){MIDIusb.sendControlChange(64,120,channelselect+1);}
      else{MIDIusb.sendControlChange(64,20,channelselect+1);}
      //printBin16(binario);
  }
  if((flagh[15]==1)&& (currentMicros >= ftimer[15])&&(digitalRead(key_pin[12])==1)){
      flagh[15] = 0;
      ftimer[15] = currentMicros;
      Serial.println("Sustain released: ");
  }

// Chanel pressed
  if((digitalRead(key_pin[18])==0)&&(flagh[17]==0)&& (currentMicros >= (ftimer[17]+1000))){
    lastcalled[17] = currentMicros;
    ftimer[17] = lastcalled[17] + bptime;
    binario|= BLOCKED;
    flagh[17] = 1;
    printBin16(binario);
    Serial.println("Chanel pressed: ");
  }
  if((flagh[17]==1)&& (currentMicros >= ftimer[17])&&(digitalRead(key_pin[18])==1)){
    flagh[17] = 0;  
    binario &= ~BLOCKED;
    ftimer[17] = currentMicros;
    //printBin16(binario);
    Serial.println("Chanel released: ");
  }
  for (int d = 0; d < 16; d++) {
    uint8_t temp=digitalRead(key_pin[d]);
    if((noteactive[d]==0)&&(temp==0)&&(currentMicros >= (btimer[d]+1000))&&(flagh[17]==1)){
      lastpressed[d] = currentMicros;
      noteactive[d]=1;
      btimer[d] = lastpressed[d] + bptime;
      channelselect=d;
      Serial.print("channel change ");
      Serial.println(channelselect+1);
    }
    if((noteactive[d]==1)&& (currentMicros >= btimer[d])&&(digitalRead(key_pin[d])==1)&&(flagh[17]==1)){
      noteactive[d] = 0;
      btimer[d] = currentMicros;
      Serial.print("channel changed to: ");
      Serial.println(channelselect+1);
    }
  }
}  
struct Task {
    void (*fn)(void); 
    uint32_t period_us;
    uint32_t last_run;
};
Task taskr[] = {
    { key_check,0,0}, 
    { menu_check,3000,30},
    { analread_task,1000,511 }, 
    { collect,  20000,1603 },
    { encoder_check,25000,833 },
    //{ add_yours_here,  5000,   1122 },   
};
void setup() {
  TinyUSBDevice.setManufacturerDescriptor("MusicBoxProductions");
  TinyUSBDevice.setProductDescriptor("Midiboy");
  MIDIusb.begin(MIDI_CHANNEL_OMNI);  // Initiate MIDI communications, listen to all channels
  MIDIusb.turnThruOff();             // turn off echo
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  adc_init();
  adc_gpio_init(26);
  adc_gpio_init(27);
  adc_gpio_init(28);
  setNotes(REFERENCE_NOTE); 
  for (int z = 0; z < 20; z++) {
    pinMode(z, INPUT_PULLUP);
    //pinMode(z,INPUT);
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
