/* GPSSentences -- Generate some standard GPS sentences 
 *
 * Copyright 2016-2019 Baptiste PELLEGRIN
 * 
 * This file is part of GNUVario.
 *
 * GNUVario is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNUVario is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>

#include <GPSSentence.h>
#include <DebugConfig.h>
#include <HardwareConfig.h>

#ifdef HAVE_SDCARD
File fileIgc;
IGCHeader header;
IGCSentence igc;
GPSSentence igcSD;
int8_t sdcardState = SDCARD_STATE_INITIAL;

/***************/
/* gps objects */
/***************/
#ifdef HAVE_GPS

NmeaParser nmeaParser;
#endif //HAVE_GPS

#endif //HAVE_SDCARD	


/********************/
/* IGC sentence "B" */
/********************/
uint8_t GPSSentence::begin(double baroAlti) {

  return 'B';
}

void GPSSentence::writePosition(kalmanvert kalmanvert) {
  if( sdcardState == SDCARD_STATE_READY ) {
#ifdef VARIOMETER_SDCARD_SEND_CALIBRATED_ALTITUDE
#ifdef SDCARD_DEBUG
  SerialPort.println("fileIGC : write");
#endif //SDCARD_DEBUG

  fileIgc.write( igc.begin( kalmanvert.getCalibratedPosition() ) );
#else
#ifdef SDCARD_DEBUG
  SerialPort.println("fileIGC : write");
#endif //SDCARD_DEBUG

#ifdef SDCARD_DEBUG
  SerialPort.println("fileIGC : write Get position");
#endif //SDCARD_DEBUG

  fileIgc.write( igc.begin( kalmanvert.getPosition() ) );
#endif
      }

}

void GPSSentence::writeGGA(void) {
#ifdef SDCARD_DEBUG
	SerialPort.print("fileIGC GGA : ");
#endif //SDCARD_DEBUG
	
	while( igc.available() ) {
		uint8_t tmpigc = igc.get();
		
#ifdef SDCARD_DEBUG
		SerialPort.print(char(tmpigc));
#endif //SDCARD_DEBUG
		
		fileIgc.print( char(tmpigc) );
	}
	
#ifdef SDCARD_DEBUG
		SerialPort.println("");
#endif //SDCARD_DEBUG
	
}

bool GPSSentence::CreateIgcFile(void) {

    /* build date : convert from DDMMYY to YYMMDD */
    uint8_t dateChar[8]; //two bytes are used for incrementing number on filename
    uint8_t* dateCharP = dateChar;
    uint32_t date = nmeaParser.date;
    for(uint8_t i=0; i<3; i++) {
      uint8_t num = ((uint8_t)(date%100));
      dateCharP[0] = (num/10) + '0';
      dateCharP[1] = (num%10) + '0';
      dateCharP += 2;
      date /= 100;
    }

    char fileName[13];

    for(int i=0; i<6; i++) fileName[i] = dateChar[i];
    int fileNameSize = 8;
    for(int fileNumber = 0; fileNumber<LF16_FILE_NAME_NUMBER_LIMIT; fileNumber++) {
    
      /* build file name */
      int digit = fileNumber;
      int base = LF16_FILE_NAME_NUMBER_LIMIT/10;
      for(int i = fileNameSize - LF16_FILE_NAME_NUMBER_SIZE; i<fileNameSize; i++) {
        fileName[i] = '0' + digit/base;
        digit %= base;
        base /= 10;
      }
      fileName[8]  = '.';
      fileName[9] = 'I';  
      fileName[10] = 'G';  
      fileName[11] = 'C';  
      fileName[12] = '\0';        
      if (!SDHAL.exists(fileName)) break;
    }
    
#ifdef SDCARD_DEBUG
      SerialPort.print("File name : ");
      SerialPort.println((char*)fileName);
#endif //SDCARD_DEBUG

    /* create file */    
    fileIgc = SDHAL.open((char*)fileName, FILE_WRITE);
    if (fileIgc) {
      sdcardState = SDCARD_STATE_READY;
#ifdef SDCARD_DEBUG
      SerialPort.println("createSDCardTrackFile : Write Header ");
#endif //SDCARD_DEBUG
     


      /* write the header */
      int16_t datePos = header.begin();
      if( datePos >= 0 ) {
				for (int i=0; i < 4;i++) {
#ifdef SDCARD_DEBUG
					SerialPort.print(headerStrings[i]);
#endif //SDCARD_DEBUG
					fileIgc.print(headerStrings[i]);
				}

        /* write date : DDMMYY */
        uint8_t* dateCharP = &dateChar[4];
        for(int i=0; i<2; i++) {
          fileIgc.write(char(dateCharP[0]));
#ifdef SDCARD_DEBUG
          SerialPort.print(char(dateCharP[0]));
#endif //SDCARD_DEBUG          

          fileIgc.write(char(dateCharP[1]));

#ifdef SDCARD_DEBUG
          SerialPort.println(char(dateCharP[1]));
#endif //SDCARD_DEBUG          
          
          dateCharP -= 2;
        }

				for (int i=4; i < HEADER_STRING_COUNT; i++) {
#ifdef SDCARD_DEBUG
					SerialPort.print(headerStrings[i]);
#endif //SDCARD_DEBUG
					fileIgc.print(headerStrings[i]);
				}

			}
		 
      /* write the header *
      int16_t datePos = header.begin();
      if( datePos >= 0 ) {
        while( datePos ) {
					
					uint8_t tmpchar = header.get();
		
#ifdef SDCARD_DEBUG
					SerialPort.print(char(tmpchar));
#endif //SDCARD_DEBUG
					
          fileIgc.write(char(tmpchar));
          datePos--;
        }

				fileIgc.flush();
				
#ifdef SDCARD_DEBUG
        SerialPort.println("");
        SerialPort.print("Write date to IgcFile : ");
#endif //SDCARD_DEBUG          
        /* write date : DDMMYY *
        uint8_t* dateCharP = &dateChar[4];
        for(int i=0; i<3; i++) {
          fileIgc.write(char(dateCharP[0]));
#ifdef SDCARD_DEBUG
          SerialPort.print(char(dateCharP[0]));
#endif //SDCARD_DEBUG          

          fileIgc.write(char(dateCharP[1]));

#ifdef SDCARD_DEBUG
          SerialPort.println(char(dateCharP[1]));
#endif //SDCARD_DEBUG          
          
          header.get();
          header.get();
          dateCharP -= 2;
        }
            
        while( header.available() ) {
					uint8_t tmpchar = header.get();
					
#ifdef SDCARD_DEBUG
					SerialPort.print(char(tmpchar));
#endif //SDCARD_DEBUG
					
          fileIgc.write(char(tmpchar));
        }				
      }*/
			
			fileIgc.flush();
			
			return true;
    } else {
#ifdef SDCARD_DEBUG
      SerialPort.println("createSDCardTrackFile : SDCARD_STATE_ERROR ");
#endif //SDCARD_DEBUG
      sdcardState = SDCARD_STATE_ERROR; //avoid retry 
			return false;
    }
}


/*
writer.write_headers({
    'manufacturer_code': 'XCS',
    'logger_id': 'TBX',
    'date': datetime.date(1987, 2, 24),
    'fix_accuracy': 50,
    'pilot': 'Tobias Bieniek',
    'copilot': 'John Doe',
    'glider_type': 'Duo Discus',
    'glider_id': 'D-KKHH',
    'firmware_version': '2.2',
    'hardware_version': '2',
    'logger_type': 'LXNAVIGATION,LX8000F',
    'gps_receiver': 'uBLOX LEA-4S-2,16,max9000m',
    'pressure_sensor': 'INTERSEMA,MS5534A,max10000m',
    'competition_id': '2H',
    'competition_class': 'Doubleseater',
})


AXCSTBX
HFDTE870224
HFFXA050
HFPLTPILOTINCHARGE:Tobias Bieniek
HFCM2CREW2:John Doe
HFGTYGLIDERTYPE:Duo Discus
HFGIDGLIDERID:D-KKHH
HFDTM100GPSDATUM:WGS-1984
HFRFWFIRMWAREVERSION:2.2
HFRHWHARDWAREVERSION:2
HFFTYFRTYPE:LXNAVIGATION,LX8000F
HFGPSuBLOX LEA-4S-2,16,max9000m
HFPRSPRESSALTSENSOR:INTERSEMA,MS5534A,max10000m
HFCIDCOMPETITIONID:2H
HFCCLCOMPETITIONCLASS:Doubleseater

const char IGCHeader00[] PROGMEM = "AXXX ";
// vario model name
const char IGCHeader02[] PROGMEM = "\r\nHFDTE";
const char IGCHeader03[] PROGMEM = "010100";
const char IGCHeader04[] PROGMEM = "\r\nHFPLTPILOTINCHARGE: ";
//pilot name
const char IGCHeader06[] PROGMEM = "\r\nHFGTYGLIDERTYPE: ";
//glider type
const char IGCHeader08[] PROGMEM = "\r\nHFDTM100GPSDATUM: WGS-1984";
const char IGCHeader09[] PROGMEM = "\r\nHFFTYFRTYPE: ";
// vario model name
const char IGCHeader11[] PROGMEM = "\r\n";

#define HEADER_STRING_COUNT 12
const char* headerStrings[] = {	IGCHeader00,
																NULL,
																IGCHeader02,
																IGCHeader03,
																IGCHeader04,
																NULL,
																IGCHeader06,
																NULL,
																IGCHeader08,
																IGCHeader09,
																NULL,
																IGCHeader11};

*/
