# IR Remote Testing Workflow

This document explains how to test IR remote functionality without affecting the main project.

## Setup

### 1. Hardware Setup
- Connect an IR receiver module to GPIO2 (or change `IR_RECEIVE_PIN` in `ir_test.cpp`)
- Common IR receiver modules: TSOP4838, TSOP38238, etc.
- Wiring:
  - VCC → 3.3V
  - GND → GND  
  - OUT → GPIO2

### 2. Testing Commands

#### To run IR packet sniffer:
```bash
pio run -e ir_test -t upload
pio device monitor
```

#### To return to main project:
```bash
pio run -e nodemcuv2 -t upload
```

#### To switch between git branches:
```bash
# Switch to IR testing branch
git checkout ir-remote-testing

# Switch back to main project
git checkout main
```

## Workflow

1. **Start IR Testing:**
   - `git checkout ir-remote-testing`
   - `pio run -e ir_test -t upload`
   - `pio device monitor`

2. **Sniff IR Data:**
   - Point your remote at the IR receiver
   - Press buttons and note the protocol, value, and raw data
   - Document which button corresponds to which values

3. **Return to Main Project:**
   - `git checkout main`
   - `pio run -e nodemcuv2 -t upload`

4. **Implement IR Control:**
   - Add IR functionality to `main.cpp`
   - Use the sniffed data to map buttons to actions

## File Structure

- `src/main.cpp` - Your main NeoPixel project (unchanged)
- `src/ir_test.cpp` - IR packet sniffer for testing
- `platformio.ini` - Contains both environments

## Environment Configuration

- `nodemcuv2` - Main project environment
- `ir_test` - IR testing environment (excludes main.cpp, includes ir_test.cpp)

This setup allows you to:
- Keep your main project completely untouched
- Test IR functionality independently
- Easily switch between testing and main project
- Use git branches to track your IR development work
