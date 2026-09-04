#include <Arduino.h>
#include <BleKeyboard.h>

BleKeyboard bleKeyboard("esp-touch", "Espressif", 100);

const int PIN_VOL_UP     = 4;  
const int PIN_VOL_DOWN   = 15; 
const int PIN_PLAY_PAUSE = 27; 
const int PIN_NEXT       = 32; 
const int PIN_PREV       = 13; 

int touchThreshold = 30; 

bool isVolUpTouched  = false;
bool isVolDownTouched = false;
bool isPlayPauseTouched = false;
bool isNextTouched = false;
bool isPrevTouched = false;

void setup() {
    Serial.begin(115200);
    delay(1000);

    bleKeyboard.begin();

    // Calibration
    Serial.println("Calibrating...");
    long sum = 0;
    sum += touchRead(PIN_VOL_UP);
    sum += touchRead(PIN_VOL_DOWN);
    sum += touchRead(PIN_PLAY_PAUSE);
    sum += touchRead(PIN_NEXT);
    sum += touchRead(PIN_PREV);

    touchThreshold = (sum / 5) * 0.6;
    
    Serial.print("Average ambient value: "); Serial.println(sum / 5);
    Serial.print("Touch Threshold set to: "); Serial.println(touchThreshold);
    Serial.println("Ready! Pair with 'esp-touch'");
}

void loop() {

    if (bleKeyboard.isConnected()) {
        
        // VOL
        int r4 = touchRead(PIN_VOL_UP);
        if (r4 < touchThreshold && !isVolUpTouched) {
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            isVolUpTouched = true;
            Serial.println("Action: Vol Up");
        } else if (r4 >= touchThreshold) isVolUpTouched = false;

        // VOL DOWN
        int r12 = touchRead(PIN_VOL_DOWN);
        if (r12 < touchThreshold && !isVolDownTouched) {
            bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
            isVolDownTouched = true;
            Serial.println("Action: Vol Down");
        } else if (r12 >= touchThreshold) isVolDownTouched = false;

        // PLAY / PAUSE
        int r15 = touchRead(PIN_PLAY_PAUSE);
        if (r15 < touchThreshold && !isPlayPauseTouched) {
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            isPlayPauseTouched = true;
            Serial.println("Action: Play/Pause");
        } else if (r15 >= touchThreshold) isPlayPauseTouched = false;

        // NEXT TRACK
        int r27 = touchRead(PIN_NEXT);
        if (r27 < touchThreshold && !isNextTouched) {
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            isNextTouched = true;
            Serial.println("Action: Next");
        } else if (r27 >= touchThreshold) isNextTouched = false;

        // PREVIOUS TRACK
        int r32 = touchRead(PIN_PREV);
        if (r32 < touchThreshold && !isPrevTouched) {
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            isPrevTouched = true;
            Serial.println("Action: Prev");
        } else if (r32 >= touchThreshold) isPrevTouched = false;

    }

    //
    delay(10);
}
