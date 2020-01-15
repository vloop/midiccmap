// A program to map midi control changes to RPN or NRPN
// owes much to alsarawmidiin.c

// Coding Marc Périlleux 2019
// Inspiration: Jean-Michel Thiémonge, Jérôme Benhaïm

/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


// The following note is copied verbatim from alsarawmidiin.c
//
// Programmer:    Craig Stuart Sapp <craig@ccrma.stanford.edu>
// Creation Date: Sat May  9 22:03:40 PDT 2009
// Last Modified: Sat May  9 22:03:46 PDT 2009
// Filename:      alsarawmidiin.c
// Syntax:        C; ALSA 1.0
// $Smake:        gcc -o %b %f -lasound
//
// Description:	  Receive MIDI data from a synthesizer using the ALSA rawmidi 
//                interface.  Reversed engineered from amidi.c (found in ALSA 
//                utils 1.0.19 program set).
//
// First double-check that you have the ALSA system installed on your computer
// by running this command-line command:
//    cat /proc/asound/version
// Which should return something like:
//   Advanced Linux Sound Architecture Driver Version 1.0.17.
// This example program should work if the version number (1.0.17 in this
// case) is "1".
//
// Online documentation notes:
// 
// http://www.alsa-project.org/alsa-doc/alsa-lib/rawmidi.html
//
// Using SND_RAWMIDI_NONBLOCK flag for snd_rawmidi_open() or
// snd_rawmidi_open_lconf() instruct device driver to return the -EBUSY
// error when device is already occupied with another application. This
// flag also changes behaviour of snd_rawmidi_write() and snd_rawmidi_read()
// returning -EAGAIN when no more bytes can be processed.
//
// http://www.alsa-project.org/alsa-doc/alsa-lib/group___raw_midi.html
//
// int snd_rawmidi_open(snd_rawmidi_t** input, snd_rawmidi_t output, 
//                                             const char* name, int mode)
//    intput   == returned input handle (NULL if not wanted)
//    output   == returned output handle (NULL if not wanted)
//    name     == ASCII identifier of the rawmidi handle, such as "hw:1,0,0"
//    mode     == open mode (see mode descriptions above):
//                SND_RAWMIDI_NONBLOCK, SND_RAWMIDI_APPEND, SND_RAWMIDI_SYNC
//
// int snd_rawmidi_close(snd_rawmidi_t* rawmidi)
//    Close a deviced opended by snd_rawmidi_open().  Returns an negative 
//    error code if there was an error closing the device.
//
// int snd_rawmidi_write(snd_rawmidi_t* output, char* data, int datasize)
//    output   == midi output pointer setup with snd_rawmidi_open().
//    data     == array of bytes to send.
//    datasize == number of bytes in the data array to write to MIDI output.
//
// const char* snd_strerror(int errornum)
//    errornum == error number returned by an ALSA snd__* function.
//    Returns a string explaining the error number.
//

#include <alsa/asoundlib.h>     /* Interface to the ALSA system */
#include <unistd.h> /* for usleep */
#include <stdlib.h> /* for strtoul */

// Setting inBuffer size to 1 resulted in data loss
// when using virtual midi port.
// We need to expect several bytes in a single read.
#define buf_size (1024)

int verbose=0;

// We need to map 128 possible MIDI controller numbers
// What about a per channel map?
// TODO: use a struct
#define map_size (128)
enum MapType {NONE, NRPN, RPN, CC, PB}; // What about LSB/MSB?
const char *mapNames[]={"NONE", "NRPN", "RPN", "CC", "PB"};
int mapFromDefault[]={0, 0, 0, 0, 8192};
int mapToMax[]={127, 16383, 16383, 127, 16383};
int mapToDefault[]={127, 16383, 16383, 127, 16383}; // Default to max
// CC mapping
enum MapType mapType[map_size];
// unsigned char mapLSB[map_size];
// unsigned char mapMSB[map_size];
unsigned int mapNum[map_size];
int mapValFrom[map_size], mapValTo[map_size];
// After-touch mapping
enum MapType atType;
// unsigned char atLSB;
// unsigned char atMSB;
unsigned int atNum;
int atValFrom, atValTo;

// function declarations:
void errormessage(const char *format, ...);

///////////////////////////////////////////////////////////////////////////
/*
example;
a.out -v -v 1 2 -r 3 4 -c 5 6 7 8 -n 9 0x0A 0x0B 12
will set verbose to 2 (print all messages)
and will send:
- cc 1 to nrpn 2 (default behaviour)
- cc 3 to rpn 4
- cc 5 to cc 6 and cc 7 to cc 8
- cc 9 to nrpn 10 and cc 11 to nrpn 12
- all other midi messages unchanged
*/
// TODO handle ctl-C
// TODO look for default config file ~/midiccmap.ini
// before handling options, because explicit -f will
// merge and replace map
// TODO dump merged map (also if -f is given multiple times)
// TODO maybe Config file ~ TOML ?
/* Example of TOML-like config (not implemented!)
nrpn = [
 [1, 2],
 [9, 10],
 [11, 12]
]
rpn = [
 [3, 4]
]
cc = [
 [5, 6],
 [7, 8]
]
 */
// for now we use case sensitive sections, no brackets on map lines, commas optional
// Example: see midiccmap.ini

int usage(const char * command){
	printf("Use: %s [-option]... [cc value]...\n", command);
	printf("Options:\n");
	printf("-v\t\tverbose (can be specified multiple times)\n");
	printf("-h\t\tdisplay this help message\n");
	printf("-n\t\ttreat the following as cc/nrpn pairs\n");
	printf("-r\t\ttreat the following as cc/rpn pairs\n");
	printf("-c\t\ttreat the following as cc/cc pairs\n");
	printf("-f file\t\tread map from the specified file\n");
	printf("cc is a midi controller number (0 to 127)\n");
	printf("value is destination:\n");
	printf("\t0 to 127 for cc to cc mapping\n");
	printf("\t0 to 16383 for cc to rpn/nrpn mapping\n");
	printf("cc and values are in decimal or in hex with 0x prefix\n");
	printf("please note that cc values are only 7 bits, therefore\n");
	printf("even though rpn/nrpn/pitch bend are 14 bit values\n");
	printf("only 7 bits of the remapped value are significant.\n");
}

int set_maps(const enum MapType m, const unsigned cc_from, const unsigned map_to, const long val_from, const long val_to){
	int maxVal;
	if (cc_from>=map_size){
		errormessage("Error: invalid source controller number %u", cc_from);
		return(-1);
	}
	switch (m) {
		case CC:
		case RPN:
		case NRPN:
			maxVal=mapToMax[m];
			if(map_to>maxVal){
				errormessage("Error: invalid destination parameter number %u", map_to);
				return(-1);
			}
			break;
		case PB:
			maxVal=mapToMax[m];
			// map_to is irrelevant for pitch bend, no need to check
			break;
		case NONE:
			maxVal=0;
			break;
		default:
			errormessage("Error: invalid map type %u", m);
			return(-1);
	}
	if(verbose){
	    if (m==PB){
			printf("cc %u (0x%02x) to type %u %s from %d to %d\n", cc_from, cc_from, m, mapNames[m],
				val_from, val_to);
		}else{
			printf("cc %u (0x%02x) to type %u %s number %u (0x%02x) from %d to %d\n", cc_from, cc_from, m, mapNames[m],
				map_to, map_to, val_from, val_to);
		}
	}
	if(mapType[cc_from]!=NONE){
		errormessage("Warning: duplicate definition for cc %u", cc_from);
	}
	
	// Scaling values below are deliberately not checked.
	// Scaling values outside normal parameter ranges are allowed.
	// This allows extra sensitivity to low input values,
	// at the expense of precision.
	// If scaling results in an out of range output value,
	// it will be clipped to legal output range.
	if(((val_from<0)&&(val_to<0))||((val_from>maxVal)&&(val_to>maxVal))){
		errormessage("Error: unusable output range %d .. %d for cc %u\n", val_from, val_to, cc_from);
		return(-1);
	}
	if((val_from<0)||(val_to<0)||(val_from>maxVal)||(val_to>maxVal)){
		errormessage("Warning: output for cc %u will be clipped", cc_from);
	}
	
	mapType[cc_from]=m;
	mapNum[cc_from]=map_to;
	mapValFrom[cc_from]=val_from;
	mapValTo[cc_from]=val_to;
	return(0);
}

int init_maps(){
	for(int i=0; i<map_size; i++){
		set_maps(NONE, i, 0, 0, 0);
	}
}

int dump(const unsigned char *buffer, const int count) {
	for(int i=0; i<count; i++){
		printf("%3u ", buffer[i]);
	}
	fflush(stdout);
}

int midiSend(snd_rawmidi_t* midiout, const char *outBuffer, const unsigned int count, unsigned char *status){
	// Should we update runningStatusOut here, or in the calling code?
	int writeStatus;
	if ((writeStatus = snd_rawmidi_write(midiout, outBuffer, count)) < 0) {
		errormessage("Problem writing MIDI Output: %s", snd_strerror(writeStatus));
		exit(-1);
	};
	if(status){ // Update output status
		// We know that in this application the status is in outBuffer[0]
		// but the loop keeps the function more generic
		for(int i=count-1; i>=0; i--){
			if (outBuffer[i] & 0x80){
				*status=outBuffer[i];
				break;
			}
		}
	}
	if(verbose>1){
		printf(" --> ");
		dump(outBuffer, count);
	}
}

int readIniFile(const char *filename){
	printf("Reading file %s\n", filename);
	enum MapType currentType;
	unsigned long n1, n2;
	char *tail;

	FILE *fp;
	char *line = NULL;
	char *start;
	size_t len = 0;
	ssize_t read;

	fp = fopen(filename, "r");
	if (fp == NULL){
		errormessage("Error: cannot open file %s", filename);
		exit(EXIT_FAILURE);
	}
	currentType = NONE;
	const char *sectionNames[]={"None\n", "[CcToNrpn]\n", "[CcToRpn]\n", "[CcToCc]\n", "[CcToPb]\n"};
	while ((read = getline(&line, &len, fp)) != -1) {
		int valFrom, valTo;
		int valFrom0, valTo0;
		if (len>0){
			// printf("Retrieved line of length %zu:\n", read);
			start=line;
			while(*start==' ' || *start=='\t') start++;
			if (start[0]!='#' && start[0]!=';'){ // Skip comment lines
				if (start[0]=='['){ // section header									
					currentType = NONE;
					// todo: case insensitive, ignore trailing blanks (needs custom stricmp)
					if (strcmp(start, sectionNames[NRPN])==0){ currentType = NRPN;
					}else if (strcmp(start, sectionNames[RPN])==0){ currentType = RPN;
					}else if (strcmp(start, sectionNames[CC])==0){ currentType = CC;
					}else if (strcmp(start, sectionNames[PB])==0){ currentType = PB;
					}else printf("Warning: skipping section %s\n", start);
				}else if (currentType != NONE){ // map data: source destination
					// Special source: aftertouch
					int atSource;
					if (strncmp(start, "AT", 2)==0) {
						atSource=1;
						start+=2;
					}else{
						atSource=0;
						// Read the cc we are mapping from
						n1=strtoul(start, &tail, 0);
						start=tail;
					}
					
					// Read the cc/rpn/nrpn we are mapping to (except for pitch bend)
					if(currentType!=PB){
						while(*start==' ' || *start=='\t') start++;
						if (*start==',') start++; // optional comma separator
						n2=strtoul(start, &tail, 0);
						start=tail;
					}else{
						n2=0; // Will not be used anyway
					}
					
					// Read optional (should be signed?) range start and end values
					// Ranges outside output values range result in value clipping
					while(*start==' ' || *start=='\t') start++;
					if (*start==',') start++; // accept trailing comma
					valFrom=mapFromDefault[currentType];
					valFrom0=strtol(start, &tail, 0);
					if (tail!=start){
						valFrom=valFrom0;
						start=tail;
					}
					while(*start==' ' || *start=='\t') start++;
					if (*start==',') start++; // accept trailing comma
					valTo=mapToDefault[currentType];
					valTo0=strtol(start, &tail, 0);
					if (tail!=start){
						valTo=valTo0;
						start=tail;
					}

					// Check line termination
					while(*start==' ' || *start=='\t') start++;
					if (*start==',') start++; // accept trailing comma
					while(*start==' ' || *start=='\t') start++;
					if (*start && *start != '#' && *start != ';' && *start != '\n'){
						errormessage("Error: unexpected data \"%s\"", start);
						exit(-1);
					}else{
						if(atSource){
							// TODO move to set_at_map
							atType=currentType;
							atNum=n2;
							atValFrom=valFrom;
							atValTo=valTo;
							if(verbose)
								if (atType==PB){
									printf("aftertouch to type %u %s values from %d to %d\n",
										atType, mapNames[atType], atValFrom, atValTo);
								}else{
									printf("aftertouch to type %u %s number %u (0x%02x) values from %d to %d\n",
										atType, mapNames[atType], atNum, atNum, atValFrom, atValTo);
								}
						}else{
							if (set_maps(currentType, n1, n2, valFrom, valTo)){
								errormessage("Error: invalid mapping, aborting");
								exit(-1);
							}
						}
					}
				}
			}
		}
	}

	fclose(fp);
	if (line) free(line);
}

int main(int argc, char *argv[]) {
	int openStatus=0, writeStatus=0, readStatus=0; // Status returned by open, write and read
	unsigned char runningStatusIn=0; // Current MIDI Status from input stream
	unsigned char runningStatusOut=0; // Current MIDI Status in output stream
	unsigned char newStatusOut; // Future MIDI Status in output stream
	// Output (running) status can be different from last input status
	// This occurs when mapping cc to pitch, and when mapping from aftertouch
	unsigned char ccNum, channel; // MIDI controller number and value
	unsigned char atVal; // After-touch value (7 bits)
	int pbVal; // Pitch bend value; unsigned offset by 8192, not signed, keep sign for clipping only
	int parmVal; // RPN or NRPN value, unsigned, keep sign for clipping only
	int ccVal; // Control change, keep sign for clipping only
	// int mode = SND_RAWMIDI_SYNC; // don't use, see below
	int mode = SND_RAWMIDI_NONBLOCK;
	enum readStates {PASSTHRU, PROCESS_CC, PROCESS_PARM, PROCESS_VAL, PROCESS_PB, PROCESS_AT} readState;
	snd_rawmidi_t* midiin = NULL;
	snd_rawmidi_t* midiout = NULL;

	init_maps();
	atType=NONE;
	
	int i=1;
	int need_map=0;
	int currentType=NRPN;
	unsigned long n1, n2;
	char *tail;
	// Process command-line options
	while (i<argc){
		int cc, nrpn;
	    if (argv[i][0]=='-'){
			if (need_map){
				errormessage("Error: expecting map value, not %s", argv[i]);
				exit(-1);			
			}
			switch (argv[i][1]) {
				case 'v':
					verbose++;
					break;
				case 'h':
					usage(argv[0]);
					exit(0); // break;
				// 'n' 'r' 'c' 'p': NRPN, RPN, CC or pitch bend mapping
				case 'n':
					currentType=NRPN;
					break;
				case 'r':
					currentType=RPN;
					break;
				case 'c':
					currentType=CC;
					break;
				case 'p':
					currentType=PB;
					break;
				case 'f':
				    i++;
				    if (i>=argc){
						errormessage("Error: missing filename");
						exit(-1);
					}
					readIniFile(argv[i]);
					break;
				default:
					errormessage("Error: Unknown option %s", argv[i]);
					usage(argv[0]);
					exit(-1);
			}
		}else{ // No leading dash, must be (positive) numbers
			if(need_map){
				n2=strtoul(argv[i], &tail, 0);
				if (*tail){
					errormessage("Error: invalid destination \"%s\"", argv[i]);
					exit(-1);
				}
				// TODO allow range specification instead of defaults also on command line ?
				int valFrom, valTo;
				valFrom=mapFromDefault[currentType];
				valTo=mapToDefault[currentType];
				if (set_maps(currentType, n1, n2, valFrom, valTo)){
					errormessage("Error: invalid mapping, aborting");
					exit(-1);
				}
				need_map=0;
			}else{
				// TODO allow aftertouch source also on command line ?
				n1=strtoul(argv[i], &tail, 0);
				if (*tail){
					errormessage("Error: invalid source controller number%s", argv[i]);
					exit(-1);
				}
				need_map=1;
			}			
		}
		i++;
	}
	if (need_map){
		errormessage("Ignoring unexpected trailing parameter: %s", argv[i-1]);
		// exit(-1);
	}
	fflush(stdout);
	

	if ((openStatus = snd_rawmidi_open(&midiin, &midiout, "virtual", mode)) < 0) {
		errormessage("Problem opening MIDI input: %s", snd_strerror(openStatus));
		exit(1);
	}

	int count = 0; // Current count of bytes received.
	// int status_count = 0;         // Current count of status bytes received.
	// int total_count = 0;
	// int data_count; // Number of data bytes following MIDI status byte
	char inBuffer[buf_size];        // Storage for input buffer
	unsigned char outBuffer[buf_size]; // cc 99, 98, 6, optionally 38, + reset rpn
	int k; // Index in outBuffer
	// printf("\nEAGAIN: %d %s %s", EAGAIN, snd_strerror(-EAGAIN), snd_strerror(EAGAIN));
	// -> EAGAIN=11 Resource temporarily unavailable
	// fflush(stdout);
	// readStatus = snd_rawmidi_read(midiin, &inBuffer[count], 1); // -11 on non-blocking, 1 on blocking
	// readStatus = snd_rawmidi_read(midiin, &inBuffer[count], 3); // 3 on blocking
	// if(readStatus>0) count=readStatus;
	// printf("\n#read readStatus %d %s\n", readStatus, snd_strerror(readStatus));
	// fflush(stdout);
	// for(int i=0; i<count; i++){
	//	   printf("%u ", (unsigned char)inBuffer[i]);
	// };
	readState = PASSTHRU;

	while (1) {
		/* MIDI read, blocking version - don't use
		// blocking mode on "virtual" drops bytes ???
		while (count<buf_size && readStatus == 1) {
			readStatus = snd_rawmidi_read(midiin, &inBuffer[count++], 1);
		}
		printf("\n>read readStatus: %d %s, count: %d\n", readStatus, snd_strerror(readStatus), count);
		*/
		
		// MIDI read, non-blocking version
		count = 0;
		readStatus = snd_rawmidi_read(midiin, inBuffer, buf_size);
		while (readStatus == -EAGAIN) { // Keep polling
			usleep(320); // One physical MIDI byte (10 bits at 31250 bps)
			readStatus = snd_rawmidi_read(midiin, inBuffer, buf_size);
		}
		// snd_rawmidi_drain(midiin); // Stuck on C0 anyway ??
		// printf("\n>read status: %d %s, count: %d\n", readStatus, readStatus>0?"ok":snd_strerror(readStatus), count);
		if (readStatus>0){
			count = readStatus; // Not pretty
		}else{
			errormessage("Problem reading MIDI input: %s", snd_strerror(readStatus));
			break;
		}
		// total_count+=count;
		
		if(verbose>1){
			printf("\n[%u]", count);
			dump(inBuffer, count);
			fflush(stdout);
		}

		for(int i=0; i<count; i++){
			if (inBuffer[i] & 0x80){ // Received status byte, 80..FF
		        if(verbose>1) printf("S");
				runningStatusIn=inBuffer[i];
				channel = runningStatusIn & 0x0F;
				// data_count=data_length[(runningStatusIn & 0x70)>>4];
				switch(runningStatusIn & 0xF0) {
					case 0xB0:
						readState = PROCESS_CC;
						break;
					case 0xD0:
						readState = PROCESS_AT;
						break;
					default:
						readState = PASSTHRU;
				}
			}else{ // Received data byte, 00..7F
		        if (verbose>1) printf("D");
		        switch (readState){
				case PASSTHRU:
					break;
				case PROCESS_CC: // Got a cc number
					// Next state depends on map type
					if(verbose>1) printf("1");
					ccNum=inBuffer[i];
					if(mapType[ccNum] == NONE){ // No mapping
						// catch up with status
						midiSend(midiout, &runningStatusIn, 1, &runningStatusOut);
						if(verbose>1) printf("s");

						// Send unchanged cc number
						midiSend(midiout, &inBuffer[i], 1, &runningStatusOut);
						readState = PROCESS_VAL;
					}else if(mapType[ccNum] == NRPN || mapType[ccNum] == RPN){
						readState = PROCESS_PARM;
						// Do nothing until value is received
					}else if(mapType[ccNum] == CC){
						k=0;
						// Catch up with status
						outBuffer[k++]=runningStatusIn;
						if(verbose>1) printf("s");
						
						// Send remapped cc
						outBuffer[k++]=mapNum[ccNum] & 0x7F;
						midiSend(midiout, outBuffer, k, &runningStatusOut);

						if(verbose>1) printf("c 0x%02x ", mapNum[ccNum]);
						readState = PROCESS_VAL; // Next byte will be cc value
						continue; // Done processing cc number
					}else if(mapType[ccNum] == PB){
						readState = PROCESS_PB;
					}else{
						errormessage("Internal error - unknown cc map type %u\n", mapType[ccNum]);
						exit(-1);
					}
					break;
				case PROCESS_PARM: // RPN/NRPN mapping specific
					if(verbose>1) printf("2");
					ccVal=inBuffer[i];
					parmVal=mapValFrom[ccNum]+ccVal*(mapValTo[ccNum]-mapValFrom[ccNum])/127;
					if (parmVal<0) parmVal=0;
					if (parmVal>16383) parmVal=16383;
					// see https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
					k=0;
					newStatusOut=0xB0+channel;
					if(newStatusOut!=runningStatusOut){
					    outBuffer[k++]=newStatusOut;
					}
					outBuffer[k++]=(mapType[ccNum] == RPN)?0x65:0x63;
					outBuffer[k++]=(mapNum[ccNum]>>7)&0x7F; // mapMSB[ccNum];
					// outBuffer[3]=0xB0+channel; // Unneeded, unchanged running status
					outBuffer[k++]=(mapType[ccNum] == RPN)?0x64:0x62;
					outBuffer[k++]=mapNum[ccNum]&0x7f; // mapLSB[ccNum];
					// outBuffer[6]=0xB0+channel; // Unneeded, unchanged running status
					outBuffer[k++]=0x06; // Data entry MSB
					outBuffer[k++]=(parmVal>>7)&0x7F;
					outBuffer[k++]=0x26; // Data entry LSB
					outBuffer[k++]=parmVal&0x7F;
					// The following prevent accidental change of NRPN value
					outBuffer[k++]=0x65; // RPN MSB
					outBuffer[k++]=0x7F;
					outBuffer[k++]=0x64; // RPN LSB
					outBuffer[k++]=0x7F;
					// Luckily running status is still 0xBcc, no need to resend
					midiSend(midiout, outBuffer, k, &runningStatusOut);
					readState = PROCESS_CC;
					break;
				case PROCESS_VAL:
				    // CC mapping specific, send value
					// Like passthru except a cc num could follow (running status is 0xB_ )
					// Hence next state PROCESS_CC
					ccVal=mapValFrom[ccNum]+inBuffer[i]*(mapValTo[ccNum]-mapValFrom[ccNum])/127;
					if (ccVal<0) ccVal=0;
					if (ccVal>127) ccVal=127;
					outBuffer[0]=ccVal;
					midiSend(midiout, outBuffer, 1, &runningStatusOut);
					readState = PROCESS_CC;
					break;
				case PROCESS_PB:
				    // Pitch bend mapping specific, send scaled value
				    // Signed pitched change is represented by an unsigned with offset +8192
				    // i.e. values below 8192 are interpreted as negative by synths
					if(verbose>1) printf("P");
					ccVal=inBuffer[i];
					pbVal=mapValFrom[ccNum]+ccVal*(mapValTo[ccNum]-mapValFrom[ccNum])/127;
					if (pbVal<0) pbVal=0;
					if (pbVal>16383) pbVal=16383;
					k=0;
					newStatusOut=0xE0+channel;
					if(newStatusOut!=runningStatusOut){
						outBuffer[k++]=newStatusOut;
						// runningStatusOut=newStatusOut;
					}
					outBuffer[k++]=pbVal & 0x7F;
					outBuffer[k++]=(pbVal >>7) & 0x7F;
					// At this stage output running status is E0
					// while input running status is B0 (or D0)
					// We should echo the input running status after sending the pitch bend
					// or not depending on what follows in the output stream
					// This is handled through newStatusOut
					midiSend(midiout, outBuffer, k, &runningStatusOut);
					// We came here by processing a cc, more cc data bytes can follow
					readState = PROCESS_CC;
					break;
				case PROCESS_AT:
					if(verbose>1) printf("A");
					atVal=inBuffer[i];
					// TODO: Factor out
					// sendParm(snd_rawmidi_t* midiout, unsigned char &runningStatusOut, unsigned char channel, MapType type, unsigned int parmNum, unsigned int parmVal, int fromVal, int toVal)
					// sendCc(snd_rawmidi_t* midiout, unsigned char &runningStatusOut, unsigned char channel, unsigned char ccNum, unsigned char ccVal, int fromVal, int toVal)
					// sendPb(snd_rawmidi_t* midiout, unsigned char &runningStatusOut, unsigned char channel, unsigned int pbVal, int fromVal, int toVal)
					k=0;
					switch (atType){
						case NONE:
							newStatusOut=runningStatusIn;
							if(newStatusOut!=runningStatusOut){
								outBuffer[k++]=newStatusOut;
							}
							outBuffer[k++]=atVal;
							break;
						case CC:
							ccVal=atValFrom+atVal*(atValTo-atValFrom)/127;
							if (ccVal<0) ccVal=0;
							if (ccVal>127) ccVal=127;
							newStatusOut=0xB0+channel;
							if(newStatusOut!=runningStatusOut){
								outBuffer[k++]=newStatusOut;
							}
						    outBuffer[k++]=atNum;
						    outBuffer[k++]=ccVal;
							break;
						case RPN:
						case NRPN:
							parmVal=atValFrom+atVal*(atValTo-atValFrom)/127;
							if (parmVal<0) parmVal=0;
							if (parmVal>16383) parmVal=16383;
							newStatusOut=0xB0+channel;
							if(newStatusOut!=runningStatusOut){
								outBuffer[k++]=newStatusOut;
							}
							outBuffer[k++]=(atType == RPN)?0x65:0x63;
							outBuffer[k++]=(atNum>>7)&0x7F;
							outBuffer[k++]=(atType == RPN)?0x64:0x62;
							outBuffer[k++]=atNum&0x7F;
							outBuffer[k++]=0x06; // Data entry MSB
							outBuffer[k++]=(parmVal>>7)&0x7F;
							outBuffer[k++]=0x26; // Data entry LSB
							outBuffer[k++]=parmVal&0x7F;
							// The following prevent accidental change of NRPN value
							outBuffer[k++]=0x65; // RPN MSB
							outBuffer[k++]=0x7F;
							outBuffer[k++]=0x64; // RPN LSB
							outBuffer[k++]=0x7F;
							break;
						case PB:
							pbVal=atValFrom+ccVal*(atValTo-atValFrom)/127; // ?? 128
							if (pbVal<0) pbVal=0;
							if (pbVal>16383) pbVal=16383;
							newStatusOut=0xE0+channel;
							if(newStatusOut!=runningStatusOut){
								outBuffer[k++]=newStatusOut;
							}
							outBuffer[k++]=pbVal & 0x7F;
							outBuffer[k++]=pbVal >>7;
							break;
						default:
							errormessage("Internal error - unknown aftertouch map type %u\n", atType);
							exit(-1);
					}
					// We'll never need to resend input status:
					// - if next input message is aftertouch it will map to the same output status
					// - if not it will already have an explicit status
					midiSend(midiout, outBuffer, k, &runningStatusOut);
					readState = PROCESS_AT; // Handle input running status, ready to receive more aftertouch data
					break;
				default:
					errormessage("Internal error - unexpected read state %u", readState);
					exit(-1);
				
				} // End of switch readState
			}
			if (readState == PASSTHRU){
				midiSend(midiout, &inBuffer[i], 1, &runningStatusOut);
				/*
				if ((writeStatus = snd_rawmidi_write(midiout, &inBuffer[i], 1)) < 0) {
			        errormessage("Problem writing MIDI Output: %s", snd_strerror(writeStatus));
					exit(-1);
		        }
		        */
		        if(inBuffer[i]&0x80){
					// runningStatusOut=inBuffer[i];
					if(verbose){
						printf("s");
						fflush(stdout);
					}
				}else{
					if(verbose){
						printf(".");
						fflush(stdout);
					}
				}
			}
		}
		// count=0;
	} // End of main while (1) loop

//	printf("\nTotal:%5u\n", total_count);

	snd_rawmidi_close(midiin);
	midiin  = NULL;    // snd_rawmidi_close() does not clear invalid pointer,
	return 0;          // so might be a good idea to erase it after closing.
}

///////////////////////////////////////////////////////////////////////////

//////////////////////////////
//
// error -- print error message
//

void errormessage(const char *format, ...) {
   va_list ap;
   va_start(ap, format);
   vfprintf(stderr, format, ap);
   va_end(ap);
   putc('\n', stderr);
}


