#include "Arduino.h"

class UserLED {
    public:
        UserLED(uint8_t pin) : led_pin(pin) {}

        void init() {
            pinMode(led_pin, OUTPUT); // Set the LED pin as output
            set(false); // Initialize the LED to off
        }

        // Turns the status LED on or off depending on the provided state boolean
        void set(bool state) {
            digitalWrite(led_pin, !state); // Set LED state
        }

    private:
        uint8_t led_pin;
};