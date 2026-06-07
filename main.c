// Configuration bits for PIC16F1947
#pragma config FOSC = INTOSC   // Oscillator Selection (Internal oscillator)
#pragma config WDTE = OFF      // Watchdog Timer (Disabled)
#pragma config PWRTE = OFF     // Power-up Timer (Disabled)
#pragma config MCLRE = ON      // MCLR Pin Function (Enabled)
#pragma config CP = OFF        // Code Protection (Disabled)
#pragma config CPD = OFF       // Data Code Protection (Disabled)
#pragma config BOREN = OFF     // Brown-out Reset (Disabled)
#pragma config CLKOUTEN = OFF  // Clock Out Enable (Disabled)
#pragma config IESO = OFF      // Internal/External Switchover (Disabled)
#pragma config FCMEN = OFF     // Fail-Safe Clock Monitor (Disabled)
#pragma config WRT = OFF       // Flash Memory Self-Write Protection (Disabled)
#pragma config STVREN = ON     // Stack Overflow/Underflow Reset (Enabled)
#pragma config BORV = LO       // Brown-out Reset Voltage Selection (Low)
#pragma config LVP = OFF       // Low-Voltage Programming (Disabled for High Voltage ICSP)
#pragma config PLLEN = ON      // Enable the 4x PLL

#define _XTAL_FREQ 32000000    // Update delay macros for 32MHz

#include <xc.h>
#include <stdint.h>


#define XPIN_SCORE_ROLL_CANDIDATE_LOWER_THRESHOLD       800000L
#define XPIN_REFRESH_FREQUENCY                          400     // This value can be 100 to 1000 Hz
#define XPIN_WATCHDOG_FREQUENCY                         150     // This value is used to ensure the MPU is still running
#define TICKS_PER_SECOND                                15      // Used as an animation reference

// Set this to 0 and the displays will boot blank
// and set it to 1 to boot to a 7s animation
#define XPIN_SHOW_SPLASH_SCREEN         1

#define XPIN_NO_HOST_FOUND              0
#define XPIN_BASIC_HOST_FOUND           1
#define XPIN_SMART_HOST_FOUND           2
#define XPIN_NATIVE7_HOST_FOUND         3



// LED Pin Definitions
//#define RED_LED   LATEbits.LATE6
//#define GREEN_LED LATEbits.LATE7

// Global variables (volatile because they are modified in interrupts)
volatile uint8_t CachedBCD[5] = {0x0F, 0x0F, 0x0F, 0x0F, 0x0F};
volatile uint8_t DisplayBuffer[5][7];
volatile uint8_t HostDetected = XPIN_NO_HOST_FOUND;
volatile uint8_t EmptyStrobesSeen = 0;
volatile uint32_t LastBlankingSeen = 0;
volatile uint32_t TicksSinceBoot = 0;

uint16_t DisplayOutputTickCount = 0;
uint32_t DisplayTestStartTime = 0;

// Master shadow register for PORTE. 
uint8_t PortE_Master = 0xC9;
// Initialized to 0xC8 (1100 1001): Both LEDs OFF (1), RE3 High (1), RE0-2/RE4-5 Low (0)
volatile uint8_t DisplayOutputStage = 0;
uint8_t DigitOutput = 6;
volatile uint8_t HostDetectionConfidence = 0;
volatile uint8_t HostCandidate = 0;
volatile uint8_t BlankingSignalsSeen = 0;
volatile uint8_t LastStrobeSeen = 0;
uint32_t HighScoreToDate = 0xFFFFFFFF; // This is the working high score
uint32_t NewHighestScore = 0;
uint32_t NativeMPUHSTD = 0xFFFFFFFF;

uint8_t ShowingHighScore = 0;
uint8_t InAttractMode = 1;
uint32_t CurrentMPUScores[4];
uint8_t CurrentRollDigits[4];
uint8_t CurrentBIP;
uint8_t DisplayTestPhase;

volatile uint8_t DisplayCache[5][6];
volatile uint8_t ScoreToBeEvaluated[4][6];
volatile uint8_t ScoreStable;
volatile uint8_t StableCount[5];
volatile uint8_t ScoreChanged = 0x00;
volatile uint8_t ValidDigitsSeen = 0x00;
volatile uint8_t DisplayTestMode = 0x00;
volatile uint8_t ScoreReadyForEvaluation = 0x00;
volatile uint8_t InOperatorMenu = 0x00;
volatile uint8_t SawBothTestTypes = 0x00;

volatile uint8_t ScanCompleteFlag = 0;
volatile uint8_t CapturedValidDigits = 0;
volatile uint8_t CapturedScoreStable = 0;
volatile uint8_t CapturedDisplayTestMode = 0;
volatile uint8_t GreenLedState = 1; // 1 = OFF (Active Low)
volatile uint8_t RedLedState = 1;   // 1 = OFF
volatile uint8_t InServiceMenu = 0;


// EEPROM Starting Addresses
#define EEPROM_SIG_ADDR         0x00
#define EEPROM_HSTD_ADDR        0x04
#define EEPROM_NATIVE_HSTD      0x08 // 4 bytes for the MPU's native HSTD

// Global RAM mirror for fast access during gameplay
uint32_t HSTDMemory; // This is the high score currently in eeprom

void InitializePersistentMemory() {
    uint8_t expectedSignature[4] = {0x37, 0x79, 0x6F, 0x6C};
    uint8_t needsInitialization = 0;

    // Check if the 4-byte signature matches
    for (uint8_t i = 0; i < 4; i++) {
        if (eeprom_read(EEPROM_SIG_ADDR + i) != expectedSignature[i]) {
            needsInitialization = 1;
            break;
        }
    }

    // If signature is missing or corrupt, format the EEPROM
    if (needsInitialization) {
        
        // Write the signature
        for (uint8_t i = 0; i < 4; i++) {
            eeprom_write(EEPROM_SIG_ADDR + i, expectedSignature[i]);
        }

        // Write 0xFFFFFFFF to the 4-byte HSTDMemory and NativeMPUHSTD locations
        for (uint8_t i = 0; i < 4; i++) {
            eeprom_write(EEPROM_HSTD_ADDR + i, 0xFF);
            eeprom_write(EEPROM_NATIVE_HSTD + i, 0xFF);
        }
        
        HSTDMemory = 0xFFFFFFFF;
        NativeMPUHSTD = 0xFFFFFFFF;
        
    } else {
        
        // Assemble the 32-bit value from the 4 EEPROM bytes (Little Endian)
        uint32_t tempMemory = 0;
        tempMemory |= ((uint32_t)eeprom_read(EEPROM_HSTD_ADDR + 3) << 24);
        tempMemory |= ((uint32_t)eeprom_read(EEPROM_HSTD_ADDR + 2) << 16);
        tempMemory |= ((uint32_t)eeprom_read(EEPROM_HSTD_ADDR + 1) << 8);
        tempMemory |= ((uint32_t)eeprom_read(EEPROM_HSTD_ADDR + 0));
        
        HSTDMemory = tempMemory;

        // Assemble NativeMPUHSTD from the 4 EEPROM bytes (Little Endian)
        uint32_t tempNative = 0;
        tempNative |= ((uint32_t)eeprom_read(EEPROM_NATIVE_HSTD + 3) << 24);
        tempNative |= ((uint32_t)eeprom_read(EEPROM_NATIVE_HSTD + 2) << 16);
        tempNative |= ((uint32_t)eeprom_read(EEPROM_NATIVE_HSTD + 1) << 8);
        tempNative |= ((uint32_t)eeprom_read(EEPROM_NATIVE_HSTD + 0));
        
        NativeMPUHSTD = tempNative;
    }
}




void CommitHSTDMemory(uint32_t currentHighScore, uint32_t currentNativeHSTD) {
    
    // Evaluate and write HSTDMemory
    if (currentHighScore != HSTDMemory) {
        HSTDMemory = currentHighScore;
        uint8_t newBytes[4];
        
        newBytes[0] = (uint8_t)(HSTDMemory & 0xFF);
        newBytes[1] = (uint8_t)((HSTDMemory >> 8) & 0xFF);
        newBytes[2] = (uint8_t)((HSTDMemory >> 16) & 0xFF);
        newBytes[3] = (uint8_t)((HSTDMemory >> 24) & 0xFF);

        for (uint8_t i = 0; i < 4; i++) {
            if (eeprom_read(EEPROM_HSTD_ADDR + i) != newBytes[i]) {
                eeprom_write(EEPROM_HSTD_ADDR + i, newBytes[i]);
            }
        }
    }

    // Evaluate and write NativeMPUHSTD
    if (currentNativeHSTD != NativeMPUHSTD) {
        NativeMPUHSTD = currentNativeHSTD;
        uint8_t nativeBytes[4];
        
        nativeBytes[0] = (uint8_t)(NativeMPUHSTD & 0xFF);
        nativeBytes[1] = (uint8_t)((NativeMPUHSTD >> 8) & 0xFF);
        nativeBytes[2] = (uint8_t)((NativeMPUHSTD >> 16) & 0xFF);
        nativeBytes[3] = (uint8_t)((NativeMPUHSTD >> 24) & 0xFF);

        for (uint8_t i = 0; i < 4; i++) {
            if (eeprom_read(EEPROM_NATIVE_HSTD + i) != nativeBytes[i]) {
                eeprom_write(EEPROM_NATIVE_HSTD + i, nativeBytes[i]);
            }
        }
    }
}





void ShowScoreInAllDisplays(uint32_t scoreToShow) {
    uint8_t tempBCD[7];
    uint32_t temp = scoreToShow;
    uint8_t digit;
    uint8_t leadingZero = 1; // Flag for blanking

    // Millions (Index 6)
    digit = 0;
    while (temp >= 1000000ul) { temp -= 1000000ul; digit++; }
    if (digit == 0 && leadingZero) tempBCD[6] = 0x0F;
    else { tempBCD[6] = digit; leadingZero = 0; }

    // Hundred Thousands (Index 5)
    digit = 0;
    while (temp >= 100000ul) { temp -= 100000ul; digit++; }
    if (digit == 0 && leadingZero) tempBCD[5] = 0x0F;
    else { tempBCD[5] = digit; leadingZero = 0; }

    // Ten Thousands (Index 4)
    digit = 0;
    while (temp >= 10000ul) { temp -= 10000ul; digit++; }
    if (digit == 0 && leadingZero) tempBCD[4] = 0x0F;
    else { tempBCD[4] = digit; leadingZero = 0; }

    // Thousands (Index 3)
    digit = 0;
    while (temp >= 1000ul) { temp -= 1000ul; digit++; }
    if (digit == 0 && leadingZero) tempBCD[3] = 0x0F;
    else { tempBCD[3] = digit; leadingZero = 0; }

    // Hundreds (Index 2)
    digit = 0;
    while (temp >= 100ul) { temp -= 100ul; digit++; }
    if (digit == 0 && leadingZero) tempBCD[2] = 0x0F;
    else { tempBCD[2] = digit; leadingZero = 0; }

    // Tens (Index 1)
    digit = 0;
    while (temp >= 10ul) { temp -= 10ul; digit++; }
    if (digit == 0 && leadingZero && temp) tempBCD[1] = 0x0F;
    else { tempBCD[1] = digit; leadingZero = 0; }

    // Ones (Index 0) - Never blanked, always show at least '0'
    tempBCD[0] = (uint8_t)temp;

    // Copy the temp BCD array into Player 1, 2, 3, and 4 displays
    for (uint8_t displayIndex = 0; displayIndex < 4; displayIndex++) {
        for (uint8_t digitIndex = 0; digitIndex < 7; digitIndex++) {
            DisplayBuffer[displayIndex][digitIndex] = tempBCD[digitIndex];
        }
    }
}

void InitializeHardware(void) {
    // Initialize the display buffer to 0x0F (blank) to suppress zeros on boot
    for (uint8_t i = 0; i < 5; i++) {
        for (uint8_t j = 0; j < 7; j++) {
            DisplayBuffer[i][j] = 0x0F;
        }
    }

    // Configure internal oscillator to 8 MHz with 4x PLL enabled (32 MHz total)
    OSCCON = 0xF0;

    // PORTC (RC0-RC3) as digital inputs for incoming BCD data
    TRISC |= 0x0F; 

    // PORTB (RB0-RB5) as digital inputs for incoming Blanking & Display Latches
    TRISB |= 0x3F; 

    // PORTD (RD0-RD6) as digital inputs (RD0 is Button)
    TRISD |= 0x7F;

    // PORTD (RD7) as digital output for Blanking OUT
    TRISDbits.TRISD7 = 0;
    LATDbits.LATD7 = 0;

    // PORTA (RA0-RA3) as digital outputs for outgoing BCD
    TRISA &= 0xF0;
    ANSELA &= 0xF0;
    LATA |= 0x0F; // Hold high when idle

    // PORTE (RE0-RE5) as digital outputs for outgoing Digit Enables
    TRISE &= 0x00;      // 00000000: Clear bits 0-7 to configure as outputs
    ANSELE &= 0x00;     // Clear bits to configure as digital pins
    LATE = 0xC9;        // Turn off both LEDS (RE6 and RE7), and set digit pins RE3 and RE0 on
    
    // PORTF (RF1-RF5) as digital outputs for outgoing Display Latches
    TRISF &= 0xC1;
    ANSELF &= 0xC1;
    LATF &= 0xC1; 

    // Enable falling-edge interrupts on RB0 through RB5
    IOCBN |= 0x3F;
    IOCBP &= 0xC0; // Ensure rising-edge interrupts are disabled
    IOCBF = 0x00;  // Clear any pending IOC flags
    
    // Enable Interrupt-on-Change globally and enable peripheral interrupts
    INTCONbits.IOCIE = 1; 
    INTCONbits.PEIE = 1;  
    INTCONbits.GIE = 1;   
}

// ==============================================================================
// InitializeDisplayTimer
// ==============================================================================
// Purpose: Set up Timer2 at given frequency to output data to the displays
// 
// ==============================================================================
uint32_t InitializeDisplayTimer(uint16_t frequency, uint8_t enableInterrupts) {
    // Constrain input to the requested 100Hz - 1000Hz range
    if (frequency < 100) frequency = 100;
    if (frequency > 1000) frequency = 1000;

    // F_CY = 8,000,000 Hz at 32MHz clock
    // Using Prescaler 1:64 and Postscaler 1:4 (Total divider = 256)
    // 8,000,000 / 256 = 31250 Hz timer clock
    
    // Calculate PR2 with integer rounding: (numerator + (denominator / 2)) / denominator
    PR2 = (uint8_t)(((31250UL + (frequency / 2)) / frequency) - 1);
    
    // Reset timer value to prevent a rollover stall if the new PR2 
    // is lower than the current TMR2 count
    TMR2 = 0;
    
    // T2OUTPS = 0011 (1:4), TMR2ON = 1, T2CKPS = 11 (1:64)
    T2CON = 0x1F; 
    
    // Enable/disable Timer2 Interrupts
    PIE1bits.TMR2IE = enableInterrupts;

    // Return the actual achieved period in milliseconds using integer math
    // Equivalent to the previous float math: (PR2 + 1) * 0.032
    return (((uint32_t)(PR2 + 1) * 32) / 1000);
}

// ==============================================================================
// GetCachedScore
// ==============================================================================
// Purpose: This is a helper function to turn BCD data into a regular integer.
// 
// Restrictions: This function only works for scores. Credit or Ball In Play
// conversions are handled elsewhere
// 
// ==============================================================================
// 60-entry lookup table mapping (Digit Place x BCD Value)
const uint32_t BCDValueLUT[70] = {
    // 1s
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    // 10s
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90,
    // 100s
    0, 100, 200, 300, 400, 500, 600, 700, 800, 900,
    // 1,000s
    0, 1000ul, 2000ul, 3000ul, 4000ul, 5000ul, 6000ul, 7000ul, 8000ul, 9000ul,
    // 10,000s
    0, 10000ul, 20000ul, 30000ul, 40000ul, 50000ul, 60000ul, 70000ul, 80000ul, 90000ul,
    // 100,000s
    0, 100000ul, 200000ul, 300000ul, 400000ul, 500000ul, 600000ul, 700000ul, 800000ul, 900000ul,
    // 1,000,000s
    0, 1000000ul, 2000000ul, 3000000ul, 4000000ul, 5000000ul, 6000000ul, 7000000ul, 8000000ul, 9000000ul
};

uint32_t GetCachedScore(uint8_t displayIndex) {
    if (displayIndex > 3 || ScoreToBeEvaluated[displayIndex][0] > 9) {
        return 0xFFFFFFFF;
    }

    uint32_t score = 0;
    uint8_t tableOffset = 0;

    for (uint8_t i = 0; i < 6; i++) {
        uint8_t bcdValue = ScoreToBeEvaluated[displayIndex][i];
        
        // Stop evaluating if a blanking mask is encountered
        if (bcdValue > 9) break;

        score += BCDValueLUT[tableOffset + bcdValue];
        tableOffset += 10;
    }

    return score;
}

uint32_t GetDisplayScore(uint8_t displayIndex) {
    if (displayIndex > 3 || DisplayBuffer[displayIndex][0] > 9) {
        return 0xFFFFFFFF;
    }

    uint32_t score = 0;
    uint8_t tableOffset = 0;

    for (uint8_t i = 0; i < 7; i++) {
        uint8_t bcdValue = DisplayBuffer[displayIndex][i];
        
        // Stop evaluating if a blanking mask is encountered
        if (bcdValue > 9) break;

        score += BCDValueLUT[tableOffset + bcdValue];
        tableOffset += 10;
    }

    return score;
}

uint8_t GetBallInPlay() {
    uint8_t BIP = 0;
    volatile uint8_t *displayPtr = &DisplayBuffer[4][0];

    if (*displayPtr>0x09) return 0xFF;
    BIP += *displayPtr++;
    if (*displayPtr>0x09) return BIP;
    BIP += BCDValueLUT[10 + *displayPtr];
    return BIP;
}
 
uint8_t GetCredits() {
    uint8_t Credits = 0;
    if (DisplayBuffer[4][4]<0x0A) Credits += DisplayBuffer[4][4] * 10;
    if (DisplayBuffer[4][3]<0x0A) Credits += DisplayBuffer[4][3];
    return Credits;
}

// ==============================================================================
// SendDigitBuffer
// ==============================================================================
// Purpose: This function sends the current digit to each of the displays
// and then decrements the current digit to point at the next one that
// needs to be displayed
//
// How it works:
// The PIC is running at 250ns per instruction, so the timing of this
// data is not going to be too fast for the decoder chips, however, there is
// an additional NOP in each strobe to ensure that it's high for long enough
// for the MC14543 to register
// 
// ==============================================================================
void SendDigitBuffer() {
     
    // Start at index 6 (Millions) for left-to-right multiplexing sweep
    static uint8_t currentDigitIndex = 6;

    // Safely inject the active-low LED states into the shadow register
    if (GreenLedState == 0) PortE_Master &= ~0x80; else PortE_Master |= 0x80;
    if (RedLedState == 0) PortE_Master &= ~0x40; else PortE_Master |= 0x40;

    // Calculate the new Digit Enable lines
    PortE_Master = (PortE_Master & 0xF8) | (7 - currentDigitIndex);

    // Blanking OUT High
    LATDbits.LATD7 = 1;

    // Set the new Digit Enable lines
    LATE = PortE_Master;
    
    // Display 1 (RF1)
    LATA = (LATA & 0xF0) | (DisplayBuffer[0][currentDigitIndex] & 0x0F);
    LATFbits.LATF1 = 1;
    NOP();
    LATFbits.LATF1 = 0;

    // Display 2 (RF2)
    LATA = (LATA & 0xF0) | (DisplayBuffer[1][currentDigitIndex] & 0x0F);
    LATFbits.LATF2 = 1;
    NOP();
    LATFbits.LATF2 = 0;

    // Display 3 (RF3)
    LATA = (LATA & 0xF0) | (DisplayBuffer[2][currentDigitIndex] & 0x0F);
    LATFbits.LATF3 = 1;
    NOP();
    LATFbits.LATF3 = 0;

    // Display 4 (RF4)
    LATA = (LATA & 0xF0) | (DisplayBuffer[3][currentDigitIndex] & 0x0F);
    LATFbits.LATF4 = 1;
    NOP();
    LATFbits.LATF4 = 0;

    // Display 5 / Credit (RF5)    
    LATA = (LATA & 0xF0) | (DisplayBuffer[4][currentDigitIndex] & 0x0F);
    LATFbits.LATF5 = 1;
    NOP();
    LATFbits.LATF5 = 0;

    // Blanking OUT Low
    LATDbits.LATD7 = 0;

    // Decrement to scan left-to-right
    if (currentDigitIndex) {
        currentDigitIndex--;
    } else {
        currentDigitIndex = 6;
    }

}



uint32_t LastFrameShown = 0xFFFFFFFF;
void BlankAllDisplays() {
    // Blank all displays
    if (TicksSinceBoot==0 && LastFrameShown==0xFFFFFFFF) {
        LastFrameShown = 0;
        for (uint8_t d = 0; d < 5; d++) {
            for (uint8_t i = 0; i < 7; i++) {
                DisplayBuffer[d][i] = 0x0F;
            }
        }
    }

}

void Show7sSplashScreen() {

    if (TicksSinceBoot==0) {
        if (LastFrameShown==0xFFFFFFFF) {
            // This is the first frame, so we blank
            // all displays
            LastFrameShown = 0;
            for (uint8_t d = 0; d < 5; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    DisplayBuffer[d][i] = 0x0F;
                }
            }
        }
    } else if (TicksSinceBoot<8) {
        if (LastFrameShown!=TicksSinceBoot) {
            LastFrameShown = TicksSinceBoot;
            for (uint8_t d = 0; d < 4; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    if (i<TicksSinceBoot) DisplayBuffer[d][i] = 7;
                    else DisplayBuffer[d][i] = 0x0F;
                }
            }
        }
    } else if (TicksSinceBoot<14) {
        if (LastFrameShown!=TicksSinceBoot) {
            LastFrameShown = TicksSinceBoot;
            for (uint8_t d = 0; d < 4; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    if (i<(TicksSinceBoot-8)) DisplayBuffer[d][i] = 0x0F;
                    else DisplayBuffer[d][i] = 7;
                }
            }
        }
    } else {
        if (LastFrameShown!=TicksSinceBoot) {
            LastFrameShown = TicksSinceBoot;
            uint8_t show7Place = (uint8_t)((TicksSinceBoot-14)%12);
            if (show7Place>6) show7Place = 12-show7Place;
            for (uint8_t d = 0; d < 4; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    if (i!=show7Place) DisplayBuffer[d][i] = 0x0F;
                    else DisplayBuffer[d][i] = 7;
                }
            }
        }
    }

}

uint32_t DisplayTestTicksAtLastFrame = 0;
void ShowDisplayTest() {

    // Initial test frame
    if (DisplayTestPhase==0xFF) {
        for (uint8_t d = 0; d < 5; d++) {
            for (uint8_t i = 0; i < 7; i++) {
                DisplayBuffer[d][i] = 0x00;
            }
        }
        DisplayTestPhase = 0;
        DisplayTestTicksAtLastFrame = TicksSinceBoot;
        return;
    }

    uint8_t numberOfPhaseSteps = 9;
    if (SawBothTestTypes) numberOfPhaseSteps = 16;

    // At 2 Hz (or slightly slower), we're going to change the frame
    if ((TicksSinceBoot - DisplayTestTicksAtLastFrame)>(1 + TICKS_PER_SECOND/2)) {
        // We've advanced to the next frame
        DisplayTestTicksAtLastFrame = TicksSinceBoot;
        DisplayTestPhase += 1;
        if (DisplayTestPhase>numberOfPhaseSteps) DisplayTestPhase = 0;

        if (DisplayTestPhase<10) {
            for (uint8_t d = 0; d < 5; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    DisplayBuffer[d][i] = DisplayTestPhase;
                }
            }    
        } else {
            uint8_t digitToShow = 6-(DisplayTestPhase-10);
            for (uint8_t d = 0; d < 5; d++) {
                for (uint8_t i = 0; i < 7; i++) {
                    if (i==digitToShow) DisplayBuffer[d][i] = 8;
                    else DisplayBuffer[d][i] = 0x0F;
                }
            }    
        }
    }
}



// ==============================================================================
// IocToFlagMap[256]
// ==============================================================================
// Purpose: Acts as a zero-cost Priority Encoder and Noise Mask for the IOCBF register.
//
// How it works: 
// When multiple interrupt flags trigger simultaneously (or if bus noise occurs),
// reading the raw IOCBF register might yield a complex bitmask (e.g., 0x07).
// Instead of using slow bit-shifting or if/else chains to find which pin fired, 
// the raw 8-bit IOCBF value is used directly as the index to this array.
// 
// - It ignores Bit 0 (Blanking) and Bits 6-7 entirely.
// - It isolates Bits 1 through 5 (Display Latches).
// - If multiple latches are flagged, it strictly returns the single lowest-order 
//   active latch flag (0x02, 0x04, 0x08, 0x10, or 0x20).
// - If no valid latch flags are present, it returns 0xFF.
// ==============================================================================
const uint8_t IocToFlagMap[256] = {
    // 0x00 - 0x0F
    0xFF, 0xFF, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x10 - 0x1F
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x20 - 0x2F
    0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x30 - 0x3F
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x40 - 0x4F 
    0xFF, 0xFF, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x50 - 0x5F
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x60 - 0x6F
    0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x70 - 0x7F
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x80 - 0x8F
    0xFF, 0xFF, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0x90 - 0x9F
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xA0 - 0xAF
    0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xB0 - 0xBF
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xC0 - 0xCF
    0xFF, 0xFF, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xD0 - 0xDF
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xE0 - 0xEF
    0x20, 0x20, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02,
    // 0xF0 - 0xFF
    0x10, 0x10, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02, 0x08, 0x08, 0x02, 0x02, 0x04, 0x04, 0x02, 0x02
};


// ==============================================================================
// IocFlagToDisplayMap[33]
// ==============================================================================
// Purpose: Converts an isolated hardware latch flag into a software array index.
//
// How it works:
// Once IocToFlagMap isolates the specific power-of-2 flag (e.g., 0x08 for Latch 3),
// that isolated flag is used as the index for this array to determine which 
// DisplayBuffer row (0-4) corresponds to the hardware pin.
//
// The array is sized 33 because the highest possible valid flag is 0x20 (Decimal 32).
//
// Mappings:
// Index 2  (0x02, Latch 1) -> Display 0
// Index 4  (0x04, Latch 2) -> Display 1
// Index 8  (0x08, Latch 3) -> Display 2
// Index 16 (0x10, Latch 4) -> Display 3
// Index 32 (0x20, Latch 5) -> Display 4
// All other indices return 0xFF (Invalid).
// ==============================================================================
const uint8_t IocFlagToDisplayMap[33] = {
    0xFF, 0xFF, 0,    0xFF, 1,    0xFF, 0xFF, 0xFF, // 0 - 7
    2,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 8 - 15
    3,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 16 - 23
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // 24 - 31
    4                                               // 32
};


// ==============================================================================
// BasicHostDigitMap[256]
// ==============================================================================
// Purpose: Maps the physical PORTD state directly to the 0-indexed digit array,
// automatically correcting for the +1 physical pin shift on the hardware header.
//
// How it works:
// The 6-digit Basic Host uses a 1-hot encoding scheme for digit selection. 
// However, the hardware wiring is physically shifted up by one pin.
// 
// Instead of correcting this with slow math or conditional logic in the ISR,
// the raw 8-bit PORTD value is passed directly as the index. The array handles 
// isolating the active pin, correcting the shift, and wrapping the highest pin 
// back around to index 0.
//
// Shifted Mappings:
// Index 4   (0x04, RD2 = 1s Digit)   -> Array Index 0
// Index 8   (0x08, RD3 = 10s Digit)  -> Array Index 1
// Index 16  (0x10, RD4 = 100s Digit) -> Array Index 2
// Index 32  (0x20, RD5 = 1k Digit)   -> Array Index 3
// Index 64  (0x40, RD6 = 10k Digit)  -> Array Index 4
// Index 2   (0x02, RD1 = 100k Digit) -> Array Index 5 (Wrapped to top)
// ==============================================================================
const uint8_t BasicHostDigitMap[256] = {
    // 0x00 - 0x0F
    0xFF, 0xFF, 0x00, 0x00, 0x01, 0x01, 0xFF, 0xFF, 0x02, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x10 - 0x1F
    0x03, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x20 - 0x2F
    0x04, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x30 - 0x3F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x40 - 0x4F
    0x05, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x50 - 0x5F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x60 - 0x6F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x70 - 0x7F
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x80 - 0x8F
    0xFF, 0xFF, 0x00, 0x00, 0x01, 0x01, 0xFF, 0xFF, 0x02, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0x90 - 0x9F
    0x03, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xA0 - 0xAF
    0x04, 0x04, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xB0 - 0xBF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xC0 - 0xCF
    0x05, 0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xD0 - 0xDF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xE0 - 0xEF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    // 0xF0 - 0xFF
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};


uint8_t CountBits(uint8_t byteToBeCounted) {
    uint8_t numBits = 0;
  
    for (uint8_t count = 0; count < 8; count++) {
      numBits += (byteToBeCounted & 0x01);
      byteToBeCounted = byteToBeCounted >> 1;
    }
  
    return numBits;
}

uint8_t FirstBit(uint8_t byteToBeCounted) {
    uint8_t count = 0;
  
    for (count = 0; count < 8; count++) {
      if (byteToBeCounted&0x01) break;
      byteToBeCounted = byteToBeCounted >> 1;
    }
  
    return count+1;
}


void __interrupt() MainInterruptHandler(void) {
    uint8_t valueOfPortC = PORTC;
    
    // 1. Display Latches and Blanking handler
    if (INTCONbits.IOCIF) {
        uint8_t valueOfPortD = PORTD;
        uint8_t valueOfIOCBF = IOCBF;

        // Direct dual-LUT flag isolation and mapping
        uint8_t activeFlag = IocToFlagMap[valueOfIOCBF];
        
        if (activeFlag != 0xFF) {
            // If any of the display latches have fallen, 
            // we're going to cache the BCD data
            uint8_t currentDisplay = IocFlagToDisplayMap[activeFlag];
            IOCBF &= ~activeFlag; // turn off display latch falling edge interrupt
            if (currentDisplay!=0xFF) CachedBCD[currentDisplay] = valueOfPortC & 0x0F;
        }
    
        // If this falling edge is blanking, we have cleanup
        // to do (moving the cached BCD data to the score cache)
        if (valueOfIOCBF & 0x01) {
            // Updating the variable tells the main loop that the MPU is up and running
            LastBlankingSeen = TicksSinceBoot; 

            // Host Detection Logic Trap
            if (HostDetected == XPIN_NO_HOST_FOUND) {
                // We haven't detected the host yet, 
                // so we need to see what values are
                // present on the digit enables (if any)
                uint8_t digitPins = (valueOfPortD >> 1) & 0x3F; // Mask out RD1-RD6

                if (digitPins) {
                    if ((digitPins & 0x08) && (digitPins & 0x07)) {
                        if (HostCandidate==XPIN_SMART_HOST_FOUND) {
                            HostDetectionConfidence += 1;
                        } else {
                            HostCandidate = XPIN_SMART_HOST_FOUND;
                            HostDetectionConfidence = 1;
                        }
                    } else if (CountBits(digitPins) == 1) {
                        if (HostCandidate==XPIN_NATIVE7_HOST_FOUND) {
                            // If we already know it's a Native 7, keep building confidence
                            HostDetectionConfidence += 1;
                        } else {
                            // Default to assuming it's a Basic host until we see a blank
                            if (HostCandidate != XPIN_BASIC_HOST_FOUND) {
                                HostCandidate = XPIN_BASIC_HOST_FOUND;
                                HostDetectionConfidence = 1;
                            } else {
                                HostDetectionConfidence += 1;
                            }
                        }
                        LastStrobeSeen = FirstBit(digitPins);
                    } else {
                        // Don't recognize this pattern
                        // Unknown host type
                        HostDetectionConfidence = 0;
                        HostCandidate = XPIN_NO_HOST_FOUND;
                    }
                    EmptyStrobesSeen = 0;
                } else {
                    if (EmptyStrobesSeen==0 && LastStrobeSeen==1) {
                        // This looks like a native 7 host
                        // because we have seen strobes, but now after the 1s digit
                        // there is no strobe at all
                        if (HostCandidate!=XPIN_NATIVE7_HOST_FOUND) {
                            HostCandidate = XPIN_NATIVE7_HOST_FOUND;
                        }
                        HostDetectionConfidence += 1;
                    } else {
                        // An unexpected blank occurred in the middle of a sweep, reset
                        HostDetectionConfidence = 0;
                        HostCandidate = XPIN_NO_HOST_FOUND;
                    }
                    EmptyStrobesSeen += 1;
                }
            
                // Increase threshold to 14 to capture two full 7-digit cycles
                if (HostDetectionConfidence > 14) {
                    // Turn OFF Timer2 interrupts to protect IOC reads
                    // and set Timer2 to a lower frequency for test animations
                    // and watchdog
                    InitializeDisplayTimer(XPIN_WATCHDOG_FREQUENCY, 0);
                    GreenLedState = 0;
                    RedLedState = 1;

                    HostDetected = HostCandidate;

                    // We can clear any boot digits
                    for (uint8_t displayCount=0; displayCount<5; displayCount++) {
                        for (uint8_t digitCount=0; digitCount<7; digitCount++) {
                            DisplayBuffer[displayCount][digitCount] = 0x0F;
                        }
                    }
                }
            } else if (HostDetected) {
                // Gen-lock the output multiplexing to the MPU strobe
                SendDigitBuffer();
                DisplayOutputStage = 1;

                // The host has been found, so we can post the 
                // value to the output buffer or cache it for evaluation
                uint8_t currentDigit = 0xFF;

                if (HostDetected == XPIN_BASIC_HOST_FOUND) {
                    currentDigit = BasicHostDigitMap[valueOfPortD];
                } else if (HostDetected == XPIN_SMART_HOST_FOUND) {
                    // Construct binary index from RD1 (LSB), RD2, RD3 (MSB)
                    currentDigit = 15 - ((valueOfPortD >> 1) & 0x0F);
                } else if (HostDetected == XPIN_NATIVE7_HOST_FOUND) {
                    uint8_t digitPins = (valueOfPortD >> 1) & 0x3F; 
                    if (digitPins == 0) {
                        currentDigit = 6; // Route the missing strobe to Millions
                    } else {
                        currentDigit = BasicHostDigitMap[valueOfPortD];
                    }
                }

                // Check to see if the digit is valid
                if (currentDigit!=0xFF) {

                    uint8_t copyToDisplayBuffer = 1;
                    // We know which digit is being referenced
                    if (HostDetected==XPIN_BASIC_HOST_FOUND) {
                        if (currentDigit==5) {
                            ValidDigitsSeen = 0x0F;
                            DisplayTestMode = 0x01;
                        }
                        uint8_t digitsValidMask = 0x01;
                        uint8_t digitsStartedMask = 0x10;
                        volatile uint8_t *cacheBCDPtr = CachedBCD;
                        volatile uint8_t *lastBCDPtr = (currentDigit<5) ? &DisplayCache[0][currentDigit+1] : NULL;
                        volatile uint8_t *cacheDisplayPtr = &DisplayCache[0][currentDigit];
                        for (uint8_t displayCount=0; displayCount<5; displayCount++) {
                            uint8_t cachedDigit = *cacheBCDPtr;

                            // Now check to see if the new digits match
                            // what's in the DisplayCache (indicating a stable score)
                            StableCount[displayCount] += 1;
                            if (cachedDigit!=*cacheDisplayPtr) {
                                // This score is not stable -- zero the counter                                
                                StableCount[displayCount] = 0;
                                ScoreStable &= ~digitsValidMask;
                                *cacheDisplayPtr = cachedDigit;
                            } else if (StableCount[displayCount]>23) { // 6 digits the same 4x in a row
                                ScoreStable |= digitsValidMask;
                            }

                            // If we're not in DisplayTestMode then turn that flag off
                            if (lastBCDPtr && DisplayTestMode) {
                                if (cachedDigit!=*lastBCDPtr || (*lastBCDPtr)>9) DisplayTestMode = 0;
                                lastBCDPtr += 6;
                            }

                            if (displayCount<4) {
                                // Now, for the player scores only, we can check if the 
                                // digits we're seeing are valid
                                if (ValidDigitsSeen & digitsValidMask) {
                                    if (cachedDigit<10) {
                                        if (currentDigit == 0 && cachedDigit != 0x00) {
                                            // We're going to assume that a valid score
                                            // requires the last digit to be 0.
                                            // I don't know of a 6-digit Bally/Stern game where that's not true
                                            ValidDigitsSeen &= ~digitsValidMask; // Invalidate if 1s digit is not 0
                                        } else {
                                            ValidDigitsSeen |= digitsStartedMask;
                                        }
                                    } else {
                                        if ((ValidDigitsSeen & digitsStartedMask) || (currentDigit<=1)) {
                                            ValidDigitsSeen &= ~digitsValidMask;
                                        }
                                    }
                                }
                            }
                            digitsValidMask <<= 1;
                            digitsStartedMask <<= 1;
                            cacheBCDPtr += 1;
                            cacheDisplayPtr += 6;
                        }

                        // If this is the last digit, we can decide
                        // which scores to process further
                        if (currentDigit==0) {

                            // Detect Display Test Mode 2 (Walking 8)
                            // If Mode 1 already passed, we skip this evaluation.
                            if (DisplayTestMode == 0 && (ScoreStable & 0x01)) {
                                uint8_t validCount = 0;
                                uint8_t valSum = 0;
                                
                                // Checking Player 1's cache is sufficient for global MPU state
                                cacheDisplayPtr = &DisplayCache[0][0];
                                for (uint8_t i = 0; i < 6; i++) {
                                    if (*cacheDisplayPtr < 0x0A) {
                                        validCount++;
                                        valSum += *cacheDisplayPtr;
                                    }
                                    cacheDisplayPtr++;
                                }
                                
                                // Exactly one valid digit, and its value is 8
                                if (validCount == 1 && valSum == 8) {
                                    DisplayTestMode = 0x02;
                                }
                            }

                            // Here we're setting flags so the main
                            // loop will know if the buffer is 
                            // Valid, Stable, and/or we're in test mode
                            CapturedValidDigits = ValidDigitsSeen;
                            CapturedScoreStable = ScoreStable;
                            CapturedDisplayTestMode = DisplayTestMode;
                            ScanCompleteFlag = 1;
                        }

                        // Always put the credit/BIP cache in the DisplayCache
                        //DisplayCache[4][currentDigit] = CachedBCD[4];
                    } else {
                        // If this is NOT a BASIC host, then we're going to 
                        // take the digits from the MPU without question and
                        // put them in the DisplayBuffer
                        for (uint8_t displayCount=0; displayCount<5; displayCount++) {
                            DisplayBuffer[displayCount][currentDigit] = CachedBCD[displayCount];
                        }
                    }

                }
            }
            IOCBF &= 0xFE; // turn off blanking interrupt
        }
    }


    // 2. Timer2 Interrupt: Fallback Multiplexing & Clock
    // Only processed here if the interrupt is explicitly enabled (Host Dead)
    if (PIR1bits.TMR2IF && PIE1bits.TMR2IE) {
        PIR1bits.TMR2IF = 0; 

        DisplayOutputTickCount++;
        if (DisplayOutputTickCount >= (XPIN_REFRESH_FREQUENCY/TICKS_PER_SECOND)) {
            DisplayOutputTickCount = 0;
            TicksSinceBoot++;

            if (InServiceMenu == 0) {
                if (TicksSinceBoot&0x01) GreenLedState ^= 1; 
                RedLedState = 1; 
            }            
        }

        SendDigitBuffer();
        DisplayOutputStage = 1; 
    }

}

void ResetCurrentMPUScores() {
    for (uint8_t count=0; count<4; count++) {
        CurrentMPUScores[count] = 0;
        CurrentRollDigits[count] = 0;
    }
}

uint8_t OriginalHostState = XPIN_NO_HOST_FOUND;
void HandleServiceMenuButton() {
    // Correct active-low physical evaluation
    uint8_t buttonPressed = (PORTDbits.RD0 == 0) ? 1 : 0; 
    static uint8_t lastButtonState = 0;
    static uint32_t buttonPressStartTicks = 0;

    if (buttonPressed && !lastButtonState) {
        // Falling edge
        buttonPressStartTicks = TicksSinceBoot;
        
        if (!InServiceMenu) {
            // Enter the service menu
            InServiceMenu = 1;
            GreenLedState = 1;
            RedLedState = 0;
            OriginalHostState = HostDetected;
            HostDetected = XPIN_NATIVE7_HOST_FOUND; 
            for (uint8_t displayCount=0; displayCount<5; displayCount++) {
                for (uint8_t digitCount=0; digitCount<7; digitCount++) {
                    DisplayBuffer[displayCount][digitCount] = 0x0F;
                }
            }            
        }
    } else if (!buttonPressed && lastButtonState) {
        // Rising edge
        if (InServiceMenu == 1) {
            // Ignore the release of the initial press
            InServiceMenu = 2;
        } else if (InServiceMenu == 2) {
            // Evaluate long vs short press
            if ((buttonPressStartTicks + TICKS_PER_SECOND) < TicksSinceBoot) {
                // Long press (> 1s): Clear HSTD
                HighScoreToDate = 0xFFFFFFFF;
                NewHighestScore = 0xFFFFFFFF;
                CommitHSTDMemory(0xFFFFFFFF, 0xFFFFFFFF); // Force EEPROM commit immediately
                
                // Ensure Red LED goes back to solid ON after flashing
                RedLedState = 0;
                GreenLedState = 1;
            } else {
                // Short press (< 1s): Exit menu
                HostDetected = OriginalHostState;
                RedLedState = 1;
                GreenLedState = 0;
                InServiceMenu = 0;
            }
        }
    } else if (buttonPressed && lastButtonState) {
        // Holding
        if (InServiceMenu == 2) {
            // Flash the red LED while holding
            RedLedState = TicksSinceBoot & 0x01;
            if ((buttonPressStartTicks + TICKS_PER_SECOND) < TicksSinceBoot) {
                GreenLedState = (TicksSinceBoot & 0x01) ? 0 : 1;
            }
        }
    }

    lastButtonState = buttonPressed;
}


void ResetHostVariables() {
    DisplayOutputTickCount = 0;
    HostDetected = XPIN_NO_HOST_FOUND;
    TicksSinceBoot = 0;
    LastFrameShown = 0xFFFFFFFF;
    LastBlankingSeen = 0;
    DisplayOutputTickCount = 0;
    BlankingSignalsSeen = 0;
    EmptyStrobesSeen = 0;
    LastStrobeSeen = 0;
    HostDetectionConfidence = 0;
    HostCandidate = XPIN_NO_HOST_FOUND;
    InOperatorMenu = 0x00;
    DisplayTestStartTime = 0;

    // Enable hardware interrupts for jitter-free fallback multiplexing    
    InitializeDisplayTimer(XPIN_REFRESH_FREQUENCY, 1); 
}


int main(void) {
    InitializePersistentMemory();
    InitializeHardware();

    // Turn on the timer with a fast frequency
    // to update displays and enable interrupts
    InitializeDisplayTimer(XPIN_REFRESH_FREQUENCY, 1); 
    
    HighScoreToDate = HSTDMemory;
    NewHighestScore = HighScoreToDate;

    ResetCurrentMPUScores();
    CurrentBIP = 0;

    uint32_t lastSecondRendered = 0xFFFFFFFF; // Force render on first pass

    for (uint8_t displayCount=0; displayCount<5; displayCount++) {
        for (uint8_t digitCount=0; digitCount<6; digitCount++) {
            if (displayCount<4) ScoreToBeEvaluated[displayCount][digitCount] = 0x0F;
            DisplayCache[displayCount][digitCount] = 0x0F;
        }
        StableCount[displayCount] = 0;
    }
    ValidDigitsSeen = 0x0F;
    ScoreReadyForEvaluation = 0x00;
    ScoreStable = 0x00;
    CapturedScoreStable = 0x00;
    CapturedValidDigits = 0x00;

    /*
    // Red LED Logic (Top of the second, ~50ms pulse)
    if (bootTickCount == 0) {
        PortE_Master &= ~0x40; // Turn ON (Clear bit 6)
    } else if (bootTickCount == 23) {
        PortE_Master |= 0x40;  // Turn OFF (Set bit 6)
    }

    // Green LED Logic (Bottom of the second, ~50ms pulse)
    if (bootTickCount == 225) {
        PortE_Master &= ~0x80; // Turn ON (Clear bit 7)
    } else if (bootTickCount == 248) {
        PortE_Master |= 0x80;  // Turn OFF (Set bit 7)
    }
*/   

    ResetHostVariables();
    
    while(1) {
        // Polled Clock: Only maintain TicksSinceBoot here if the ISR isn't doing it
        if (PIR1bits.TMR2IF && !PIE1bits.TMR2IE) {
            PIR1bits.TMR2IF = 0; 

            DisplayOutputTickCount++;
            if (DisplayOutputTickCount >= (XPIN_WATCHDOG_FREQUENCY/TICKS_PER_SECOND)) {
                DisplayOutputTickCount = 0;
                TicksSinceBoot++;

                HandleServiceMenuButton();
            }
        }

        if (HostDetected && TicksSinceBoot>(LastBlankingSeen+2)) {
            // We haven't seen a BLANKING signal in more
            // than a TicksSinceBoot of a second, so let's assume
            // that the MPU has rebooted
            ResetHostVariables();
        }
        
        if (DisplayOutputStage) {
            DisplayOutputStage = 0;

            if (HostDetected == XPIN_NO_HOST_FOUND) {    
                // Splash screen sequence (before the MPU boots)
                if (XPIN_SHOW_SPLASH_SCREEN==1) {
                    Show7sSplashScreen();
                } else {
                    BlankAllDisplays();
                }
            } else if (HostDetected==XPIN_BASIC_HOST_FOUND) {
                // If this is a basic host (in other words, the 7th digit is inferred)
                // we need to track scores and HSTD
                // MPU is running so we can evaluate the high scores and such
                uint8_t ballInPlay = GetBallInPlay();
    
                if (ballInPlay>=1 && ballInPlay<=9) {
                    // if we're showing ball in play to be a single-digit, non-zero number
                    // we're most likely in attract mode
                    InAttractMode = 0;
                    if (ballInPlay==1 && CurrentBIP!=1) {
                        // Rage-Quit / Start Button Slam Intercept
                        // Instantly clear out the cached scores and rollover tracking
                        // so the drop to 00 doesn't trigger a phantom million
                        ResetCurrentMPUScores();
                    }
                    CurrentBIP = ballInPlay;
                } else {
                    if (ballInPlay!=0xFF) {
                        // Any non-blank BIP that didn't turn out to be 1-9 means
                        // we're in attract mode
                        if (InAttractMode==0) {
                            // Promote the shadow variable to the active HSTD
                            if (NewHighestScore > HighScoreToDate) {
                                HighScoreToDate = NewHighestScore;
                            }                            
                            // We're transitioning from game play mode to attract mode
                            // so we need to try to save the HighScoreToDate.
                            // If it's not changed, then the function will reject the save.
                            CommitHSTDMemory(HighScoreToDate, NativeMPUHSTD);
                        }
                        InAttractMode = 1;
                    }
                    CurrentBIP = 0;
                }

                if (ScanCompleteFlag) {
                    ScanCompleteFlag = 0;
                    if (CapturedDisplayTestMode) {
                        if (DisplayTestStartTime==0) {
                            DisplayTestStartTime = TicksSinceBoot;
                            DisplayTestPhase = 0xFF;
                        }
                        if (CapturedDisplayTestMode==0x02) SawBothTestTypes = 0x01;
                        // We're going to show the test animation
                        // This will be based on how long it has been since the display test mode
                        // started, and what type of display test we're supposed to show (just 1111111, etc., 
                        // or the one with the 8 moving through each display)
                        ShowDisplayTest();

                        // Because we saw a test pattern on the digits,
                        // se're going to consider the MPU to be in
                        // the operator menu until the MPU is reset
                        InOperatorMenu = 0x01;
                    } else {
                        if ((CapturedScoreStable&0x0F)==0x0F) {                            
                            // We're only going to update the DisplayBuffer if all the score
                            // displays have settled
                            ScoreChanged = 0x00;
                            ScoreReadyForEvaluation = 0x00;
                            uint8_t digitsValidMask = 0x01;
                            volatile uint8_t *evalPtr = &ScoreToBeEvaluated[0][0];
                            volatile uint8_t *cachePtr = &DisplayCache[0][0];
                            volatile uint8_t *displayPtr = &DisplayBuffer[0][0];
                            for (uint8_t displayCount=0; displayCount<4; displayCount++) {                                
                                if (InOperatorMenu==0 && (CapturedValidDigits & digitsValidMask)) {
                                    // If this score is valid, we'll see if it has changed
                                    uint8_t sawChange = 0;
                                    for (uint8_t digitCount=0; digitCount<6; digitCount++) {
                                        if (*evalPtr!=*cachePtr) {
                                            *evalPtr = *cachePtr;
                                            sawChange = 1;
                                        }
                                        evalPtr += 1;
                                        cachePtr += 1;
                                    }
                                    if (sawChange) ScoreChanged |= digitsValidMask;
                                    ScoreReadyForEvaluation |= digitsValidMask;
                                    displayPtr += 7;
                                } else {
                                    // Any invalid score gets copied directly to the display
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = *cachePtr++;
                                    *displayPtr++ = 0x0F; // turn off 7th digit for an invalid score
                                    evalPtr += 6;
                                }
                                digitsValidMask *= 2;
                            }

                            // All stable, invalid scores have now been copied to the DisplayBuffer
                            // and stable, valid scores are waiting in the ScoreToBeEvaluated buffer
                            // as long as at least 1 score is flagged for eval
                            if (ScoreReadyForEvaluation) {
                                DisplayTestStartTime = 0; // reset display test start because we're not in test
                                
                                // We can now check to see if HSTD is being shown
                                ShowingHighScore = ((InAttractMode == 1 || CurrentBIP == 3 || CurrentBIP == 5) && ScoreReadyForEvaluation==0x0F) ? 1 : 0;
                                uint32_t newMPUScores[4];
                                // First, make sure all changed scores are converted
                                if (ShowingHighScore) {
                                    for (uint8_t scoreIndex=0; scoreIndex<4; scoreIndex++) {
                                        newMPUScores[scoreIndex] = GetCachedScore(scoreIndex);
                                        if (scoreIndex && (newMPUScores[scoreIndex-1]!=newMPUScores[scoreIndex])) {
                                            ShowingHighScore = 0;
                                        }
                                        if (newMPUScores[0] == 0) ShowingHighScore = 0;
                                    }
                                } else {
                                    digitsValidMask = 0x01;
                                    for (uint8_t scoreIndex=0; scoreIndex<4; scoreIndex++) {
                                        if (ScoreReadyForEvaluation & digitsValidMask) newMPUScores[scoreIndex] = GetCachedScore(scoreIndex);
                                        else newMPUScores[scoreIndex] = CurrentMPUScores[scoreIndex];
                                        digitsValidMask *= 2;
                                    }
                                }

                                if (ShowingHighScore) {
                                    // SPY: Memorize exactly what the MPU thinks the HSTD is. 
                                    // This naturally updates during Attract Mode and Game Over.
                                    NativeMPUHSTD = newMPUScores[0];

                                    // We're going to put the HSTD in all high scores
                                    if (HighScoreToDate==0xFFFFFFFF) {
                                        // If the HighScoreToDate is uninitialized, we can initialize it now
                                        HighScoreToDate = newMPUScores[0];
                                        NewHighestScore = HighScoreToDate;
                                        CommitHSTDMemory(HighScoreToDate, NativeMPUHSTD);
                                    }
                                    ShowScoreInAllDisplays(HighScoreToDate);
                                } else {
                                    // We're going to evaluate scores for rollover and post changed scores 
                                    // to the DisplayBuffer
                                    uint8_t scoreBitmask = 0x01;
                                    for (uint8_t displayCount=0; displayCount<4; displayCount++) {
                                        if (ScoreReadyForEvaluation & scoreBitmask) {
                                            if (ScoreChanged & scoreBitmask) {
                                                if (CurrentMPUScores[displayCount]>XPIN_SCORE_ROLL_CANDIDATE_LOWER_THRESHOLD) {
                                                    if (newMPUScores[displayCount]<CurrentMPUScores[displayCount]) {

                                                        // HSTD Flash Protection: Compare against the spied MPU broadcast
                                                        // (only applies to MPU200 games, really)
                                                        if (newMPUScores[displayCount] != NativeMPUHSTD) {                                                        
                                                            CurrentRollDigits[displayCount] += 1;
                                                            if (CurrentRollDigits[displayCount]>9) CurrentRollDigits[displayCount] = 0;
                                                        }
                                                    }
                                                }                                    
                                                uint32_t sevenDigitScore = newMPUScores[displayCount] + BCDValueLUT[60+CurrentRollDigits[displayCount]];
                                                if (sevenDigitScore>NewHighestScore) {
                                                    NewHighestScore = sevenDigitScore;
                                                }
                                            }
                                            CurrentMPUScores[displayCount] = newMPUScores[displayCount];
            
                                            if (CurrentRollDigits[displayCount] > 0) {
                                                // Rolled case: Inject millions, and zero-pad any blanking characters
                                                DisplayBuffer[displayCount][6] = CurrentRollDigits[displayCount];
                                                DisplayBuffer[displayCount][5] = (ScoreToBeEvaluated[displayCount][5] > 9) ? 0 : ScoreToBeEvaluated[displayCount][5];
                                                DisplayBuffer[displayCount][4] = (ScoreToBeEvaluated[displayCount][4] > 9) ? 0 : ScoreToBeEvaluated[displayCount][4];
                                                DisplayBuffer[displayCount][3] = (ScoreToBeEvaluated[displayCount][3] > 9) ? 0 : ScoreToBeEvaluated[displayCount][3];
                                                DisplayBuffer[displayCount][2] = (ScoreToBeEvaluated[displayCount][2] > 9) ? 0 : ScoreToBeEvaluated[displayCount][2];
                                                DisplayBuffer[displayCount][1] = (ScoreToBeEvaluated[displayCount][1] > 9) ? 0 : ScoreToBeEvaluated[displayCount][1];
                                                DisplayBuffer[displayCount][0] = (ScoreToBeEvaluated[displayCount][0] > 9) ? 0 : ScoreToBeEvaluated[displayCount][0];
                                            } else {
                                                // Normal case: Blank millions, execute fast-copy
                                                DisplayBuffer[displayCount][6] = 0x0F;
                                                DisplayBuffer[displayCount][5] = ScoreToBeEvaluated[displayCount][5];
                                                DisplayBuffer[displayCount][4] = ScoreToBeEvaluated[displayCount][4];
                                                DisplayBuffer[displayCount][3] = ScoreToBeEvaluated[displayCount][3];
                                                DisplayBuffer[displayCount][2] = ScoreToBeEvaluated[displayCount][2];
                                                DisplayBuffer[displayCount][1] = ScoreToBeEvaluated[displayCount][1];
                                                DisplayBuffer[displayCount][0] = ScoreToBeEvaluated[displayCount][0];
                                            }                                
                                        }
                                        scoreBitmask *= 2;
                                    }                                    
                                }
    
                            }
                        }

                        if (CapturedDisplayTestMode==0 && (CapturedScoreStable==0x1F)) {
                            // Update Credit/BIP with cached values
                            volatile uint8_t *cachePtr = &DisplayCache[4][0];
                            volatile uint8_t *displayPtr = &DisplayBuffer[4][0];
                            *displayPtr++ = *cachePtr++;
                            *displayPtr++ = *cachePtr++;
                            *displayPtr++ = *cachePtr++;
                            *displayPtr++ = *cachePtr++;
                            *displayPtr++ = *cachePtr++;
                            *displayPtr++ = *cachePtr++;

                            // This is a debug thing
                            //*displayPtr = (uint8_t)(((InOperatorMenu)?1:0) | ((ShowingHighScore)?2:0) | ((ScanCompleteFlag) ? 0x04 : 0x00));
                        }
                    }

                }
            }
        }        
    }
    
    return 0;
}