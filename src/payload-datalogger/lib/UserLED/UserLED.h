#include "Arduino.h"

class UserLED {
    public:
        UserLED(uint8_t pin) : led_pin(pin) {
            pinMode(led_pin, OUTPUT);
            digitalWrite(led_pin, LOW); // Initialize LED to OFF
        }

        // Turns the status LED on or off depending on the provided state boolean
        void set(bool state) {
            digitalWrite(led_pin, !state); // Set LED state
        }

    private:
        uint8_t led_pin;
};