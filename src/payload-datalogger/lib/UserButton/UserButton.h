#include "Arduino.h"

class UserButton {
    public:
        UserButton(uint8_t pin) : button_pin(pin) {
            pinMode(button_pin, INPUT);
        }

        bool read(){
            return !digitalRead(button_pin); // Read the button state
        }

        // setup interrupt callback function
        void callback(void (*callback)()) {
            attachInterrupt(button_pin, callback, FALLING); // Attach interrupt
        }

    private:
        uint8_t button_pin;
};