// winestatemachine.h

#pragma once

// in no particular order
enum WINESTATE {
    WAITINGFORAUTOFOCUS,     // this means we have powered up but the user has not performed the required AF init phase
    AUTOFOCUSCALIBRATED,     // so the user has held the bottle in front of the camera and AF has been initialized
    BEGINPHOTOPROCESS,       // so we have taken a photo and we are waiting on a result   
    WAITINGFORBOTTLE,        // so we are in normal mode -- displaying content from the sensor and waiting for a bottle to appear
    WAITINGFORBOTTLETOBEGONE,// so either calibration is complete or we have taken a photo but now the bottle needs to go to be normal again
    BOTTLETOOCLOSE,          // so the user is holding the bottle to close to the camera based on AF calibration
    BOTTLETOOFAR,            // and now they are holding too far away
    BOTTLEJUSTRIGHT_STEP1,   // the bottle in in the distance window and the first of 0.5 seconds as passed
    BOTTLEJUSTRIGHT_STEP2,   // the bottle in in the distance window and the second of 0.5 seconds as passed
    BOTTLEJUSTRIGHT_STEP3,   // the bottle in in the distance window and the third of 0.5 seconds as passed
    BOTTLEJUSTRIGHT_ANDSTILL,// the bottle is ready for a photo!
    PHOTOTAKEN,              // the photo is taken and ready to be processed
    SYSTEMIDLE,              // after a period of time the main loop will declare system idle allowing OTA to proceed
    OTAUPDATEINPROGRESS,     // well since its time we are checking for updates and loading if available
    IDENTIFICATIONINPROG,    // we have initiated the MQTT business to identify things
    TRANSMISSIONFAILURE,     // well we just aint heard back
    WINENOTFOUND,            // well that didnt work out too well
    WINEVINTAGEUNCERTAIN,    // a little discrepency here
    WINEFOUND,               // success
    UNKNOWN,                 // not sure
};

