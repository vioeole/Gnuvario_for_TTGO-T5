#include <Arduino.h>
#include <DebugConfig.h>
#include <VarioSettings.h>
#include <HardwareConfig.h>
#include <IntTW.h>
#include <ms5611.h>
#include <vertaccel.h>
#include <LightInvensense.h>
#include <TwoWireScheduler.h>

#include <kalmanvert.h>

#include <toneHAL.h>
#include <beeper.h>

#include <SerialNmea.h>

#include <sdcardHAL.h>

#include <NmeaParser.h>
#include <LxnavSentence.h>
#include <LK8Sentence.h>
#include <IGCSentence.h>
#include <GPSSentence.h>
#include <FlightHistory.h>
#include <variostat.h>
#include <VarioButton.h>

#include <Utility.h>

#include <driver/adc.h>

/*******************/
/* Version         */
/*******************/

#define VERSION      0
#define SUB_VERSION  3
#define BETA_CODE    0
#define DEVNAME      "JPG63"
#define AUTHOR "J"    //J=JPG63  P=PUNKDUMP

/******************************************************************/
/*                          VERSION                               */
/*                           ESP32                                */
/*                  Optimisé pour TTGO-T5                         */
/*                                                                */
/*                        Historique                              */
/******************************************************************/
/* v 0.1                             beta 1 version               *
 * v 0.2     beta 1      23/06/19    debug VarioScreen            *      
 * v 0.3     beta 1      25/06/19    correction mesure tension    *
 *                                   correction mesure de vitesse *
 * v 0.3     beta 2      26/06/19    correction save IGC          *                                 
 * v 0.3                 03/07/19    ajout la coupure du son      *
 *                                                                *
*******************************************************************/

/*******************/
/* General objects */
/*******************/
#define VARIOMETER_STATE_INITIAL 0
#define VARIOMETER_STATE_DATE_RECORDED 1
#define VARIOMETER_STATE_CALIBRATED 2
#define VARIOMETER_STATE_FLIGHT_STARTED 3

#ifdef HAVE_GPS
uint8_t variometerState = VARIOMETER_STATE_INITIAL;
#else
uint8_t variometerState = VARIOMETER_STATE_CALIBRATED;
#endif //HAVE_GPS

/*****************/
/* screen        */
/*****************/

#include <varioscreenGxEPD.h>

/***************/
/* IMU objects */
/***************/
#ifdef HAVE_BMP280
Bmp280 TWScheduler::bmp280;
#else
Ms5611 TWScheduler::ms5611;
#endif
#ifdef HAVE_ACCELEROMETER
Vertaccel TWScheduler::vertaccel;
#endif //HAVE_ACCELEROMETER

//Vertaccel vertaccel;

/**********************/
/* alti/vario objects */
/**********************/
#define POSITION_MEASURE_STANDARD_DEVIATION 0.1
#ifdef HAVE_ACCELEROMETER 
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.3
#else
#define ACCELERATION_MEASURE_STANDARD_DEVIATION 0.6
#endif //HAVE_ACCELEROMETER 

kalmanvert kalmanvert;

#ifdef HAVE_SPEAKER

//#define volumeDefault 5

#include <beeper.h>

//Beeper beeper(volumeDefault);

//#define BEEP_FREQ 800
#endif //HAVE_SPEAKER

/**********************/
/* SDCARD objects     */
/**********************/

VarioSettings GnuSettings;

/************************************/
/* glide ratio / average climb rate */
/************************************/
#if defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)

/* determine history params */
#ifdef HAVE_GPS

#ifdef VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
    /* unsure period divide GPS_PERIOD */
    constexpr double historyGPSPeriodCountF = (double)(VARIOMETER_INTEGRATED_CLIMB_RATE_DISPLAY_FREQ)*(double)(GPS_PERIOD)/(1000.0);
    constexpr int8_t historyGPSPeriodCount = (int8_t)(0.5 + historyGPSPeriodCountF);

    constexpr double historyPeriodF = (double)(GPS_PERIOD)/(double)(historyGPSPeriodCount);
    constexpr unsigned historyPeriod = (unsigned)(0.5 + historyPeriodF);
#else 
    /* GPS give the period */
    constexpr int8_t historyGPSPeriodCount = 1;
    constexpr unsigned historyPeriod = GPS_PERIOD;
#endif //VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE

constexpr double historyCountF = (double)(VARIOMETER_GLIDE_RATIO_INTEGRATION_TIME)/(double)historyPeriod;
constexpr int8_t historyCount = (int8_t)(0.5 + historyCountF);
#else //!HAVE_GPS
constexpr double historyCountF = (double)(VARIOMETER_INTEGRATED_CLIMB_RATE_DISPLAY_FREQ)*(double)(VARIOMETER_CLIMB_RATE_INTEGRATION_TIME)/(1000.0);
constexpr int8_t historyCount = (int8_t)(0.5 + historyCountF);

constexpr double historyPeriodF = (double)(VARIOMETER_CLIMB_RATE_INTEGRATION_TIME)/(double)historyCount;
constexpr unsigned historyPeriod = (unsigned)(0.5 + historyPeriodF);
#endif //HAVE_GPS

/* create history */
#ifdef HAVE_GPS
SpeedFlightHistory<historyPeriod, historyCount, historyGPSPeriodCount> history;
#else
#ifdef VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
FlightHistory<historyPeriod, historyCount> history;
#endif //VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
#endif //HAVE_GPS

/* compute climb rate period count when differ from glide ratio period count */
#if defined(HAVE_GPS) && defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)
#if VARIOMETER_CLIMB_RATE_INTEGRATION_TIME > VARIOMETER_GLIDE_RATIO_INTEGRATION_TIME
#error VARIOMETER_CLIMB_RATE_INTEGRATION_TIME must be less or equal than VARIOMETER_GLIDE_RATIO_INTEGRATION_TIME
#endif
constexpr double historyClimbRatePeriodCountF = (double)(VARIOMETER_CLIMB_RATE_INTEGRATION_TIME)/(double)historyPeriod;
constexpr int8_t historyClimbRatePeriodCount = (int8_t)historyClimbRatePeriodCountF;
#endif

#endif //defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)

/***************/
/* gps objects */
/***************/
#ifdef HAVE_GPS

//NmeaParser nmeaParser;

#ifdef HAVE_BLUETOOTH
boolean lastSentence = false;
#endif //HAVE_BLUETOOTH

#endif //HAVE_GPS

/*********************/
/* bluetooth objects */
/*********************/
#ifdef HAVE_BLUETOOTH
#if defined(VARIOMETER_SENT_LXNAV_SENTENCE)
LxnavSentence bluetoothNMEA;
#elif defined(VARIOMETER_SENT_LK8000_SENTENCE)
LK8Sentence bluetoothNMEA;
#else
#error No bluetooth sentence type specified !
#endif

#ifndef HAVE_GPS
unsigned long lastVarioSentenceTimestamp = 0;
#endif // !HAVE_GPS
#endif //HAVE_BLUETOOTH

unsigned long lastDisplayTimestamp;
boolean displayLowUpdateState=true;

VarioStat flystat;

/*************************************************
Internal TEMPERATURE Sensor
/*************************************************

/* 
 *  https://circuits4you.com
 *  ESP32 Internal Temperature Sensor Example
 */

 #ifdef __cplusplus
  extern "C" {
 #endif

  uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

uint8_t temprature_sens_read();


//****************************
//****************************
void setup() {
//****************************
//****************************  

/************************/
/*  Init Pin Voltage    */
/************************/
#if defined(HAVE_POWER_ALIM) 
    pinMode(POWER_PIN, OUTPUT);
    digitalWrite(POWER_PIN, POWER_PIN_STATE);   // turn on POWER (POWER_PIN_STATE is the voltage level HIGH/LOW)
#endif  

/************************/
/*  Init Pin Voltage    */
/************************/
#if defined(HAVE_VOLTAGE_DIVISOR) 
    pinMode(VOLTAGE_DIVISOR_PIN, INPUT);
    analogReadResolution(12);

#if defined(VOLTAGE_DIVISOR_DEBUG)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_DB_11);
#endif
#endif  

  SerialPort.begin(115200);

  /*****************************/
  /* wait for devices power on */
  /*****************************/
//  delay(VARIOMETER_POWER_ON_DELAY);
  delay (5000);

/************************/
/*    BOOT SEQUENCE     */
/************************/

//#ifdef PROG_DEBUG

///  while (!SerialPort) { ;}
  char tmpbuffer[100];
  sprintf(tmpbuffer,"GNUVARIO compiled on %s\0", __DATE__); // at %s", __DATE__, __TIME__);
  SerialPort.println(tmpbuffer);
  sprintf(tmpbuffer,"VERSION %i.%i - %s\0", VERSION,SUB_VERSION,DEVNAME); 
  SerialPort.println(tmpbuffer);
  if (BETA_CODE > 0) {
    SerialPort.print("Beta ");
    SerialPort.println(BETA_CODE);
  }
  SerialPort.flush();
//#endif //PRO_DEBBUG

  
  /******************/
  /* Init Speaker   */
  /******************/
  
#if defined( HAVE_SPEAKER)
  toneHAL.init();
#endif

  /******************/
  /* Init SDCARD    */
  /******************/

#ifdef HAVE_SDCARD

  if (GnuSettings.initSettings()) {
#ifdef SDCARD_DEBUG
   SerialPort.println("initialization done.");
   SerialPort.flush();
#endif //PROG_DEBUG

   sdcardState = SDCARD_STATE_INITIALIZED;
   GnuSettings.readSDSettings();
    
#ifdef SDCARD_DEBUG
   //Debuuging Printing
   SerialPort.print("Pilot Name = ");
   SerialPort.println(GnuSettings.VARIOMETER_PILOT_NAME);
#endif //SDCARD_DEBUG

#ifdef PROG_DEBUG
   //Debuuging Printing
    SerialPort.print("Pilot Name = ");
    SerialPort.println(GnuSettings.VARIOMETER_PILOT_NAME);
#endif //PROG_DEBUG

    char __dataPilotName[GnuSettings.VARIOMETER_PILOT_NAME.length()];
    GnuSettings.VARIOMETER_PILOT_NAME.toCharArray(__dataPilotName, sizeof(__dataPilotName)+1);

#ifdef PROG_DEBUG
   //Debuuging Printing
    SerialPort.print("__dataPilotName = ");
    SerialPort.print(__dataPilotName);
    SerialPort.print(" - ");
    SerialPort.print(sizeof(__dataPilotName));
    SerialPort.print(" / ");
    SerialPort.print(GnuSettings.VARIOMETER_PILOT_NAME);
    SerialPort.print(" - ");
    SerialPort.println(GnuSettings.VARIOMETER_PILOT_NAME.length());
#endif //PROG_DEBUG

    char __dataGliderName[GnuSettings.VARIOMETER_GLIDER_NAME.length()];
    GnuSettings.VARIOMETER_GLIDER_NAME.toCharArray(__dataGliderName, sizeof(__dataGliderName)+1);

#ifdef PROG_DEBUG
   //Debuuging Printing
    SerialPort.print("__dataGliderName = ");
    SerialPort.print(__dataGliderName);
    SerialPort.print(" - ");
    SerialPort.print(sizeof(__dataGliderName));
    SerialPort.print(" / ");
    SerialPort.print(GnuSettings.VARIOMETER_GLIDER_NAME);
    SerialPort.print(" - ");
    SerialPort.println(GnuSettings.VARIOMETER_GLIDER_NAME.length());
#endif //PROG_DEBUG

    header.saveParams(VARIOMETER_MODEL_NAME, __dataPilotName, __dataGliderName);
  }
  else
  {
#ifdef HAVE_SPEAKER
    if (GnuSettings.ALARM_SDCARD) {
#ifdef SDCARD_DEBUG
      SerialPort.println("initialization failed!");
#endif //SDCARD_DEBUG

      indicateFaultSDCARD();
    }
#endif //HAVE_SPEAKER 
  }
#endif //HAVE_SDCARD

  /**************************/
  /* init Two Wires devices */
  /**************************/
  //!!!
#ifdef HAVE_ACCELEROMETER
  intTW.begin();
  twScheduler.init();
//  vertaccel.init();

  /******************/
  /* get first data */
  /******************/
  
  /* wait for first alti and acceleration */
  while( ! twScheduler.havePressure() ) { }

#ifdef MS5611_DEBUG
    SerialPort.println("première mesure");
#endif //MS5611_DEBUG

  /* init kalman filter with 0.0 accel*/
  double firstAlti = twScheduler.getAlti();
#ifdef MS5611_DEBUG
    SerialPort.print("firstAlti : ");
    SerialPort.println(firstAlti);
#endif //MS5611_DEBUG

  kalmanvert.init(firstAlti,
                  0.0,
                  POSITION_MEASURE_STANDARD_DEVIATION,
                  ACCELERATION_MEASURE_STANDARD_DEVIATION,
                  millis());

#ifdef KALMAN_DEBUG
  SerialPort.println("kalman init");
#endif //KALMAN_DEBUG
#endif //HAVE_ACCELEROMETER

#if defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)
  history.init(firstAlti, millis());
#endif //defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)

#ifdef HAVE_GPS
  serialNmea.begin(9600, true);
#ifdef GPS_DEBUG
  SerialPort.println("SerialNmea init");
#endif //GPS_DEBUG
#endif //HAVE_GPS

  /***************/
  /* init screen */
  /***************/
#ifdef HAVE_SCREEN
#ifdef SCREEN_DEBUG
  SerialPort.println("initialization screen");
  SerialPort.flush();
#endif //SCREEN_DEBUG

  screen.begin();
#endif

  /***************/
  /* init button */
  /***************/

#ifdef HAVE_BUTTON
#ifdef BUTTON_DEBUG
  SerialPort.println("initialization bouton");
  SerialPort.flush();
#endif //BUTTON_DEBUG

  VarioButton.begin();
#endif

#ifdef HAVE_SCREEN
/*----------------------------------------*/
/*                                        */
/*             DISPLAY BOOT               */
/*                                        */
/*----------------------------------------*/

#ifdef SCREEN_DEBUG
  SerialPort.println("Display boot");
#endif //SCREEN_DEBUG


  screen.ScreenViewInit(VERSION,SUB_VERSION, AUTHOR,BETA_CODE);
  screen.volLevel->setVolume(toneHAL.getVolume());

#ifdef SCREEN_DEBUG
  SerialPort.println("update screen");
#endif //SCREEN_DEBUG

  screen.schedulerScreen->enableShow();
#endif //HAVE_SCREEN

  ButtonScheduleur.Set_StatePage(STATE_PAGE_VARIO);
  /* init time */
  lastDisplayTimestamp = millis(); 
  displayLowUpdateState = true;  
}

double temprature=0;

#if defined(HAVE_SDCARD) && defined(HAVE_GPS)
void createSDCardTrackFile(void);
#endif //defined(HAVE_SDCARD) && defined(HAVE_GPS)
void enableflightStartComponents(void);

//*****************************
//*****************************
void loop() {
//****************************  
//****************************

 /* if( vertaccel.readRawAccel(accel, quat) ){
    count++;
  }*/

/*  LOW UPDATE DISPLAY */
   if( millis() - lastDisplayTimestamp > 500 ) {

     lastDisplayTimestamp = millis();
     displayLowUpdateState = true;
   }


/*******************************/
/*  Compute button             */
/*******************************/

  ButtonScheduleur.update();

  /*****************************/
  /* compute vertical velocity */
  /*****************************/

#ifdef HAVE_ACCELEROMETER
  if( twScheduler.havePressure() && twScheduler.haveAccel() ) {
    
#ifdef PROG_DEBUG
//    SerialPort.println("havePressure && haveAccel");
#endif //PROG_DEBUG

    kalmanvert.update( twScheduler.getAlti(),
                       twScheduler.getAccel(NULL),
                       millis() );
#else
  if( twScheduler.havePressure() ) {
    
#ifdef MS5611_DEBUG
//    SerialPort.println("havePressure");
#endif //MS5611_DEBUG

    kalmanvert.update( twScheduler.getAlti(),
                       0.0,
                       millis() );
#endif //HAVE_ACCELEROMETER
  

//*****************
//* update beeper *
//*****************

#ifdef HAVE_SPEAKER
		beeper.setVelocity( kalmanvert.getVelocity() );
#endif //HAVE_SPEAKER

   /* set history */
#if defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)
		history.setAlti(kalmanvert.getCalibratedPosition(), millis());
#endif

		double currentalti  = kalmanvert.getCalibratedPosition();
		double currentvario = kalmanvert.getVelocity();

    /* set screen */

    flystat.SetAlti(currentalti);
    flystat.SetVario(currentvario);

#ifdef HAVE_SCREEN
#ifdef PROG_DEBUG
 //   SerialPort.print("altitude : ");
 //   SerialPort.println((uint16_t)currentalti);
#endif //PROG_DEBUG

    screen.altiDigit->setValue((uint16_t)currentalti);
#ifdef VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
    if( history.haveNewClimbRate() ) {
      screen.varioDigit->setValue(history.getClimbRate());
    }
#else
    screen.varioDigit->setValue(currentvario);
#endif //VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE   

#if (RATIO_CLIMB_RATE > 1) 
    if( history.haveNewClimbRate() ) {
      double TmpTrend;
      TmpTrend = history.getClimbRate();
      if (displayLowUpdateState) {
        if (abs(TmpTrend) < 10) screen.trendDigit->setValue(abs(TmpTrend)); 
        else                    screen.trendDigit->setValue(9.9);
        
        if (TmpTrend == 0)     screen.trendLevel->stateTREND(0);
        else if (TmpTrend > 0) screen.trendLevel->stateTREND(1);
        else                   screen.trendLevel->stateTREND(-1);
      }
    }  
#endif // (RATIO_CLIMB_RATE > 1)
#else
#ifdef VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
    if( history.haveNewClimbRate() ) {
      screen.varioDigit->setValue(history.getClimbRate());
    }
#else
    screen.varioDigit->setValue(currentvario);
#endif //VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE
#endif //HAVE_SCREEN
     
  }

  /*****************/
  /* update beeper */
  /*****************/
#ifdef HAVE_SPEAKER
  beeper.update();
#ifdef PROG_DEBUG
//    SerialPort.println("beeper update");
#endif //PROG_DEBUG
#endif //HAVE_SPEAKER

 
  /********************/
  /* update bluetooth */
  /********************/
#ifdef HAVE_BLUETOOTH
#ifdef HAVE_GPS
  /* in priority send vario nmea sentence */
  if( bluetoothNMEA.available() ) {
    while( bluetoothNMEA.available() ) {
       serialNmea.write( bluetoothNMEA.get() );
    }
    serialNmea.release();
  }
#else //!HAVE_GPS
  /* check the last vario nmea sentence */
  if( millis() - lastVarioSentenceTimestamp > VARIOMETER_SENTENCE_DELAY ) {
    lastVarioSentenceTimestamp = millis();
#ifdef VARIOMETER_BLUETOOTH_SEND_CALIBRATED_ALTITUDE
    bluetoothNMEA.begin(kalmanvert.getCalibratedPosition(), kalmanvert.getVelocity());
#else
    bluetoothNMEA.begin(kalmanvert.getPosition(), kalmanvert.getVelocity());
#endif
    while( bluetoothNMEA.available() ) {
       serialNmea.write( bluetoothNMEA.get() );
    }
  }
#endif //!HAVE_GPS
#endif //HAVE_BLUETOOTH

   /**************/
  /* update GPS */
  /**************/
#ifdef HAVE_GPS
#ifdef HAVE_BLUETOOTH
  /* else try to parse GPS nmea */
  else {
#endif //HAVE_BLUETOOTH
    
    /* try to lock sentences */
    if( serialNmea.lockRMC() ) {
      
#ifdef GPS_DEBUG
    SerialPort.println("mneaParser : beginRMC");
#endif //GPS_DEBUG

      nmeaParser.beginRMC();      
    } else if( serialNmea.lockGGA() ) {
      
#ifdef GPS_DEBUG
    SerialPort.println("mneaParser : beginGGA");
#endif //GPS_DEBUG

      nmeaParser.beginGGA();
#ifdef HAVE_BLUETOOTH
      lastSentence = true;
#endif //HAVE_BLUETOOTH
#ifdef HAVE_SDCARD      
      /* start to write IGC B frames */
      igcSD.writePosition(kalmanvert);

#endif //HAVE_SDCARD
    }
  
    /* parse if needed */
    if( nmeaParser.isParsing() ) {

#ifdef GPS_DEBUG
      SerialPort.println("mneaParser : isParsing");
#endif //GPS_DEBUG

#ifdef SDCARD_DEBUG
      SerialPort.print("writeGGA : ");
#endif //SDCARD_DEBUG
      
      while( nmeaParser.isParsing() ) {
        uint8_t c = serialNmea.read();
        
        /* parse sentence */        
        nmeaParser.feed( c );

#ifdef HAVE_SDCARD          
        /* if GGA, convert to IGC and write to sdcard */
        if( sdcardState == SDCARD_STATE_READY && nmeaParser.isParsingGGA() ) {
          igc.feed(c);
/*          while( igc.available() ) {
            fileIgc.write( igc.get() );
          }*/
          igcSD.writeGGA();          
        }
#endif //HAVE_SDCARD
      }
      
      serialNmea.release();
      fileIgc.flush();
#ifdef SDCARD_DEBUG
      SerialPort.println("");
#endif //SDCARD_DEBUG
    
#ifdef HAVE_BLUETOOTH   
      /* if this is the last GPS sentence */
      /* we can send our sentences */
      if( lastSentence ) {
          lastSentence = false;
#ifdef VARIOMETER_BLUETOOTH_SEND_CALIBRATED_ALTITUDE
          bluetoothNMEA.begin(kalmanvert.getCalibratedPosition(), kalmanvert.getVelocity());
#else
          bluetoothNMEA.begin(kalmanvert.getPosition(), kalmanvert.getVelocity());
#endif
          serialNmea.lock(); //will be writed at next loop
      }
#endif //HAVE_BLUETOOTH
    }
    
    /***************************/
    /* update variometer state */
    /*    (after parsing)      */
    /***************************/
    if( variometerState < VARIOMETER_STATE_FLIGHT_STARTED ) {

      /* if initial state check if date is recorded  */
      if( variometerState == VARIOMETER_STATE_INITIAL ) {
        if( nmeaParser.haveDate() ) {
          
#ifdef GPS_DEBUG
          SerialPort.println("VARIOMETER_STATE_DATE_RECORDED");
#endif //GPS_DEBUG

          variometerState = VARIOMETER_STATE_DATE_RECORDED;
        }
      }
      
      /* check if we need to calibrate the altimeter */
      else if( variometerState == VARIOMETER_STATE_DATE_RECORDED ) {

#ifdef GPS_DEBUG
          SerialPort.print("NmeaParser Precision : ");
          SerialPort.println(nmeaParser.precision);
          SerialPort.print("VARIOMETER_GPS_ALTI_CALIBRATION_PRECISION_THRESHOLD : ");
          SerialPort.println(VARIOMETER_GPS_ALTI_CALIBRATION_PRECISION_THRESHOLD);        
#endif //GPS_DEBUG

        /* we need a good quality value */
        if( nmeaParser.haveNewAltiValue() && nmeaParser.precision < VARIOMETER_GPS_ALTI_CALIBRATION_PRECISION_THRESHOLD ) {

#ifdef GPS_DEBUG
          SerialPort.println("GPS FIX");
#endif //GPS_DEBUG
          
          /* calibrate */
 #ifdef HAVE_SPEAKER 
          if (GnuSettings.ALARM_GPSFIX) {
 //         toneAC(BEEP_FREQ);
            beeper.generateTone(GnuSettings.BEEP_FREQ, 200);
//          delay(200);
//          toneAC(0);
          }
 #endif //defined(HAVE_SPEAKER) 

#ifdef HAVE_SCREEN
          screen.fixgpsinfo->setFixGps();
          screen.recordIndicator->setActifGPSFIX();
        //  recordIndicator->stateRECORD();
#endif //HAVE_SCREEN
          double gpsAlti = nmeaParser.getAlti();
          kalmanvert.calibratePosition(gpsAlti);


#ifdef GPS_DEBUG
          SerialPort.print("GpsAlti : ");
          SerialPort.println(nmeaParser.getAlti());
          SerialPort.println("Kalman CalibratePosition");        
#endif //GPS_DEBUG
          
#if defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)
          history.init(gpsAlti, millis());
#endif //defined(HAVE_GPS) || defined(VARIOMETER_DISPLAY_INTEGRATED_CLIMB_RATE)

          variometerState = VARIOMETER_STATE_CALIBRATED;

#ifdef GPS_DEBUG
          SerialPort.println("GPS Calibrated");        
#endif //GPS_DEBUG

#ifdef HAVE_SDCARD 
          if (!GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START) {

#ifdef SDCARD_DEBUG
          SerialPort.println("createSDCardTrackFile");        
#endif //SDCARD_DEBUG

             createSDCardTrackFile();
          }
#endif //HAVE_SDCARD
        }
      }
      
      /* else check if the flight have started */
      else {  //variometerState == VARIOMETER_STATE_CALIBRATED
        
        /* check flight start condition */
        if( (millis() > GnuSettings.FLIGHT_START_MIN_TIMESTAMP) &&
            ((GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START) &&   
             (kalmanvert.getVelocity() < GnuSettings.FLIGHT_START_VARIO_LOW_THRESHOLD || kalmanvert.getVelocity() > GnuSettings.FLIGHT_START_VARIO_HIGH_THRESHOLD)) ||
             (!GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START)
     
  //        && (kalmanvert.getVelocity() < FLIGHT_START_VARIO_LOW_THRESHOLD || kalmanvert.getVelocity() > FLIGHT_START_VARIO_HIGH_THRESHOLD) &&
#ifdef HAVE_GPS

            &&((nmeaParser.getSpeed_no_unset() > GnuSettings.FLIGHT_START_MIN_SPEED)|| (!GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START))
#endif //HAVE_GPS
          ) {
          variometerState = VARIOMETER_STATE_FLIGHT_STARTED;
          enableflightStartComponents();
        }
      }
    }
#ifdef HAVE_BLUETOOTH
  }
#endif //HAVE_BLUETOOTH
#endif //HAVE_GPS

  /* if no GPS, we can't calibrate, and we have juste to check flight start */
#ifndef HAVE_GPS
  if( variometerState == VARIOMETER_STATE_CALIBRATED ) { //already calibrated at start 
/*    if( (millis() > GnuSettings.FLIGHT_START_MIN_TIMESTAMP) &&
        (kalmanvert.getVelocity() < GnuSettings.FLIGHT_START_VARIO_LOW_THRESHOLD || kalmanvert.getVelocity() > GnuSettings.FLIGHT_START_VARIO_HIGH_THRESHOLD) ) {
      variometerState = VARIOMETER_STATE_FLIGHT_STARTED;
      enableflightStartComponents();*/
      if( (millis() > GnuSettings.FLIGHT_START_MIN_TIMESTAMP) &&
          (((GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START) &&   
           (kalmanvert.getVelocity() < GnuSettings.FLIGHT_START_VARIO_LOW_THRESHOLD || kalmanvert.getVelocity() > GnuSettings.FLIGHT_START_VARIO_HIGH_THRESHOLD)) || 
           (!GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START))) {
        variometerState = VARIOMETER_STATE_FLIGHT_STARTED;
        enableflightStartComponents();      
    }
  }
#endif // !HAVE_GPS

#if defined(HAVE_SCREEN) && defined(HAVE_VOLTAGE_DIVISOR) 
//  int tmpVoltage = analogRead(VOLTAGE_DIVISOR_PIN);
//  if (maxVoltage < tmpVoltage) {maxVoltage = tmpVoltage;}

      /* update battery level */
#if defined(VOLTAGE_DIVISOR_DEBUG)
    int val = adc1_get_raw(ADC1_CHANNEL_7);

    SerialPort.print("Tension : ");
    SerialPort.println(val);
#endif //VOLTAGE_DIVISOR_DEBUG
    
  if (displayLowUpdateState) screen.batLevel->setVoltage( analogRead(VOLTAGE_DIVISOR_PIN) );
//  batLevel.setVoltage( maxVoltage );
//  maxVoltage = 0;

#endif //HAVE_VOLTAGE_DIVISOR

  /**********************************/
  /* update low freq screen objects */
  /**********************************/
#ifdef HAVE_SCREEN

/************************************/
/* Update Time, duration            */
/* Voltage, SatLevel                */
/************************************/

#ifdef HAVE_GPS

  if (displayLowUpdateState) {
    if (nmeaParser.haveDate()) {
      
      /* set time */
#ifdef GPS_DEBUG
      SerialPort.print("Time : ");
      SerialPort.println(nmeaParser.time);
#endif //GPS_DEBUG

      screen.screenTime->setTime( nmeaParser.time );
      screen.screenTime->correctTimeZone( GnuSettings.VARIOMETER_TIME_ZONE );
      screen.screenElapsedTime->setCurrentTime( screen.screenTime->getTime() );
    }
    
      /* update satelite count */
    screen.satLevel->setSatelliteCount( nmeaParser.satelliteCount );
#ifdef GPS_DEBUG
    SerialPort.print("Sat : ");
    SerialPort.println(nmeaParser.satelliteCount);
#endif //GPS_DEBUG
  }    
#endif //HAVE_GPS  

  /*****************/
  /* update screen */
  /*****************/
#ifdef HAVE_GPS
  /* when getting speed from gps, display speed and ratio */

  if ((variometerState >= VARIOMETER_STATE_DATE_RECORDED ) && ( nmeaParser.haveNewSpeedValue() )) {

    double currentSpeed = nmeaParser.getSpeed();
    double ratio = history.getGlideRatio(currentSpeed, serialNmea.getReceiveTimestamp());

#ifdef GPS_DEBUG
          SerialPort.print("GpsSpeed : ");
          SerialPort.println(currentSpeed);
#endif //GPS_DEBUG

     flystat.SetSpeed(currentSpeed);

    // display speed and ratio    
    if (currentSpeed > 99)      screen.speedDigit->setValue( 99 );
    else                        screen.speedDigit->setValue( currentSpeed );

    if ( currentSpeed >= GnuSettings.RATIO_MIN_SPEED && ratio >= 0.0 && ratio < GnuSettings.RATIO_MAX_VALUE  && displayLowUpdateState) {
      screen.ratioDigit->setValue(ratio);
    } else {
      screen.ratioDigit->setValue(0.0);
    }
  }
#endif //HAVE_GPS
/*  if( millis() - lastDisplayTimestamp > 1000 ) {

    lastDisplayTimestamp = millis();
    //Serial.println(intTW.lastTwError);
//    SerialPort.println(accel[2]);
    
#ifdef PROG_DEBUG
//    SerialPort.println("loop");

    SerialPort.print("Vario : ");
    SerialPort.println(kalmanvert.getVelocity());
#endif //PROG_DEBUG

#ifdef HAVE_SCREEN
    temprature += 0.1; //(temprature_sens_read() - 32) / 1.8;
    if (temprature > 99.99) temprature = 0; 
 
#ifdef PROG_DEBUG
    SerialPort.print("tenperature : ");
    SerialPort.print(temprature);
    SerialPort.println(" °C");
#endif //PRO_DEBBUG

    screen.tensionDigit->setValue(temprature);
    screen.tempratureDigit->setValue(0);
//   screen.updateData(DISPLAY_OBJECT_TEMPRATURE, temprature);
    screen.schedulerScreen->displayStep();
    screen.updateScreen(); 
#endif //HAVE_SCREEN

#ifdef HAVE_GPS    
    SerialPort.print("Time : ");
    SerialPort.println(nmeaParser.time);

    SerialPort.print("Sat : ");
    SerialPort.println(nmeaParser.satelliteCount);
    
    fileIgc.print(nmeaParser.time);
    fileIgc.print(" - ");
    fileIgc.println(kalmanvert.getVelocity());
#endif //HAVE_GPS*/

   if (displayLowUpdateState) {
      screen.recordIndicator->stateRECORD();
#ifdef PROG_DEBUG
      SerialPort.println("Record Indicator : staterecord ");
      SerialPort.print("VarioState : ");
      SerialPort.println(variometerState);
#endif //PROG_DEBUG
      
   }


   if (displayLowUpdateState) {
    
#ifdef PROG_DEBUG
      SerialPort.print("Temperature: ");
      // Convert raw temperature in F to Celsius degrees
      SerialPort.print((temprature_sens_read() - 32) / 1.8);
      SerialPort.println(" C");
#endif //PROG_DEBUG
  }
   
   displayLowUpdateState = false;

   screen.schedulerScreen->displayStep();
   screen.updateScreen(); 
#endif //HAVE_SCREEN
 // }
}

#if defined(HAVE_SDCARD) && defined(HAVE_GPS)
void createSDCardTrackFile(void) {
  /* start the sdcard record */

#ifdef SDCARD_DEBUG
      SerialPort.println("createSDCardTrackFile : begin ");
#endif //SDCARD_DEBUG
 
  if( sdcardState == SDCARD_STATE_INITIALIZED ) {

#ifdef SDCARD_DEBUG
    SerialPort.println("createSDCardTrackFile : SDCARD_STATE_INITIALIZED ");
#endif //SDCARD_DEBUG

    igcSD.CreateIgcFile();
  }
}
#endif //defined(HAVE_SDCARD) && defined(HAVE_GPS)


/*******************************************/
void enableflightStartComponents(void) {
/*******************************************/  

#ifdef SDCARD_DEBUG
      SerialPort.println("enableflightStartComponents ");
#endif //SDCARD_DEBUG

#ifdef HAVE_SPEAKER
if (GnuSettings.ALARM_FLYBEGIN) {
  for( int i = 0; i<2; i++) {
  //   toneAC(BEEP_FREQ);
 //    delay(200);
  //   toneAC(0);
     beeper.generateTone(GnuSettings.BEEP_FREQ, 200);
     delay(200);
  }
}
#endif //HAVE_SPEAKER 

  /* enable near climbing */
#ifdef HAVE_SPEAKER
//#ifdef VARIOMETER_ENABLE_NEAR_CLIMBING_ALARM
if (GnuSettings.VARIOMETER_ENABLE_NEAR_CLIMBING_ALARM) {
  beeper.setGlidingAlarmState(true);
}
//#endif

//#ifdef VARIOMETER_ENABLE_NEAR_CLIMBING_BEEP
if (GnuSettings.VARIOMETER_ENABLE_NEAR_CLIMBING_BEEP) {
  beeper.setGlidingBeepState(true);
}
//#endif
#endif //HAVE_SPEAKER

#if defined(HAVE_SDCARD) && defined(HAVE_GPS) 
//&& defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)
if (GnuSettings.VARIOMETER_RECORD_WHEN_FLIGHT_START) {
  
#ifdef SDCARD_DEBUG
          SerialPort.println("createSDCardTrackFile");        
#endif //SDCARD_DEBUG

  createSDCardTrackFile();
}
#endif // defined(HAVE_SDCARD) && defined(VARIOMETER_RECORD_WHEN_FLIGHT_START)

#ifdef SDCARD_DEBUG
  SerialPort.println("Record Start");        
#endif //SDCARD_DEBUG

  screen.recordIndicator->setActifRECORD();
  screen.recordIndicator->stateRECORD();
}

//$GNGGA,064607.000,4546.2282,N,00311.6590,E,1,05,2.6,412.0,M,0.0,M,,*77
//$GNRMC,064607.000,A,4546.2282,N,00311.6590,E,0.76,0.00,230619,,,A*7D
