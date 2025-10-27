#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

// Note: This uses the IRremoteESP8266 library (crankyoldgit version)

// IR Receiver pin (you can change this to any GPIO pin)
#define IR_RECEIVE_PIN 12  // GPIO on NodeMCU

// Create IR receiver object
IRrecv irrecv(IR_RECEIVE_PIN);

// Results from IR receiver
decode_results results;

void setup() {
  Serial.begin(115200);
  Serial.println("\nIR Remote Packet Sniffer");
  Serial.println("=========================");
  Serial.println("Point your remote at the IR receiver and press buttons.");
  Serial.println("Raw data will be displayed below:\n");
  
  // Start IR receiver
  irrecv.enableIRIn();
  
  Serial.println("Ready to receive IR signals...\n");
}

void loop() {
  // Check if IR data is available
  if (irrecv.decode(&results)) {
    // Print raw timing data
    Serial.println("Raw IR Data:");
    Serial.print("Protocol: ");
    Serial.println(typeToString(results.decode_type));
    Serial.print("Value: ");
    Serial.println(results.value, HEX);
    Serial.print("Bits: ");
    Serial.println(results.bits);
    
    // Print raw timing data (useful for debugging)
    Serial.println("Raw timing data:");
    Serial.print("Raw length: ");
    Serial.println(results.rawlen);
    
    Serial.print("Raw data: ");
    for (uint16_t i = 1; i < results.rawlen; i++) {
      Serial.print(results.rawbuf[i] * kRawTick, DEC);
      if (i < results.rawlen - 1) Serial.print(", ");
    }
    Serial.println();
    
    // Print formatted data
    Serial.println("Formatted data:");
    Serial.println(resultToHumanReadableBasic(&results));
    
    Serial.println("----------------------------------------\n");
    
    // Resume receiving
    irrecv.resume();
  }
  
  delay(100);
}
