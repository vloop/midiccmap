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

// Setting buffer side to 1 resulted in data loss
// when using virtual midi port
// We need to expect several bytes in a single read
#define buf_size (1024)

int verbose=0;

// We need to map 128 possible MIDI controller numbers
// What about a per channel map?
#define map_size (128)
enum MapType {NONE, NRPN, RPN, CC, PB}; // What about LSB/MSB?
const char *mapNames[]={"NONE", "NRPN", "RPN", "CC", "PB"};
// CC mapping
enum MapType mapType[map_size];
unsigned char mapLSB[map_size];
unsigned char mapMSB[map_size];
// After-touch mapping
enum MapType atType;
unsigned char atLSB;
unsigned char atMSB;

// function declarations:
void errormessage(const char *format, ...);

///////////////////////////////////////////////////////////////////////////
/*
example;
a.out -v -v 1 2 -r 3 4 -c 5 6 7 8 -n 9 0x0A 0x0B 12
will set verbose to 2 (print all messages)
and will send:
- cc 1 to nrpn 2 (comportement par défaut)
- cc 3 to rpn 4
- cc 5 to cc 6 and cc 7 to cc 8
- cc 9 to nrpn 10 and cc 11 to nrpn 12
- all other midi messages unchanged
*/
// TODO maybe Config file ~ TOML ?
/* Example
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
// for now use case sensitive sections, no brackets on map lines, commas optional
/* Example
[NRPN]
1, 2
9, 0x0A
[RPN]
3, 4
[CC]
5, 6
7, 8
*/

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
	exit(-1);
}

int set_maps(const enum MapType m, const unsigned long cc_from, const unsigned long map_to){
	// FIXME add scaling
	if (cc_from>=map_size){
		errormessage("Error: invalid source controller number %u", cc_from);
		return(-1);
	}
	switch (m) {
		case CC:
		    if(map_to>127){
				errormessage("Error: invalid destination controller number %u", map_to);
				return(-1);
			}
			break;
		case RPN:
		case NRPN:
			if(map_to>16383){
				errormessage("Error: invalid destination parameter number %u", map_to);
				return(-1);
			}
			break;
		case PB:
			if(map_to>8191){ // FIXME shouldn't use map_to as scale
				errormessage("Error: invalid pitch bend range %u", map_to);
				return(-1);
			}
			break;
		case NONE:
			break;
		default:
			errormessage("Error: invalid map type %u", m);
			return(-1);
	}
	mapType[cc_from]=m;
	mapLSB[cc_from]=map_to & 0x7F;
	mapMSB[cc_from]=map_to >> 7;
	if(verbose)
		printf("cc %u (0x%02x) to type %u %s %s %u (0x%02x)\n", cc_from, cc_from, m, mapNames[m],
		    (m==PB)?"scale":"value", // FIXME shouldn't use map_to as scale
		    map_to, map_to);
	return(0);
}

int init_maps(){
	for(int i=0; i<map_size; i++){
		set_maps(NONE, i, 0);
	}
}

int dump(const unsigned char *buffer, const int count) {
	for(int i=0; i<count; i++){
		printf("%3u ", buffer[i]);
	}
	fflush(stdout);
}

int main(int argc, char *argv[]) {
	int status; // status returned by open, write and read
	unsigned char runningStatus; // Current MIDI status
	unsigned char ccNum, ccVal, channel; // MIDI controller number and value
	unsigned char atVal; // After-touch value
	unsigned int pbval; // Pitch bend value
	int scale; // FIXME Scaling currently for pitch bend only
	// int mode = SND_RAWMIDI_SYNC; // don't use, see below
	int mode = SND_RAWMIDI_NONBLOCK;
	enum readStates {PASSTHRU, PROCESS_CC, PROCESS_PARM, PROCESS_VAL, PROCESS_PB, PROCESS_AT} readState;
	snd_rawmidi_t* midiin = NULL;
	snd_rawmidi_t* midiout = NULL;

	init_maps();
	atType=NONE;
	
	int i=1;
	int need_map = 0;
	int currentType=NRPN;
	long int parsed;
	unsigned long n1, n2;
	char *tail;
	char * filename;
	// Process command-line options
	while (i<argc){
		// printf("\n%u %s", i, argv[i]);
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
					break;
				// 'n' 'r' 'c' NRPN, RPN or CC mapping
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
					// TODO move file read to separate function
					filename=argv[i];
					printf("Reading file %s\n", filename);
					
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
						if (len>0){
							// printf("Retrieved line of length %zu:\n", read);
							start=line;
							while(*start==' ' || *start=='\t') start++;
							if (start[0]!='#'){ // Skip comment lines
								if (start[0]=='['){ // section header									
									currentType = NONE;
									// todo: case insensitive, ignore trailing blanks (needs custom stricmp)
									if (strcmp(start, sectionNames[1])==0){ currentType = NRPN;
									}else if (strcmp(start, sectionNames[2])==0){ currentType = RPN;
									}else if (strcmp(start, sectionNames[3])==0){ currentType = CC;
									}else if (strcmp(start, sectionNames[4])==0){ currentType = PB;
									}else printf("Warning: skipping section %s\n", start);
								}else if (currentType != NONE){ // map data: source destination
									// Special source: aftertouch
									int atSource;
									if (strncmp(start, "AT", 2)==0) {
										atSource=1;
										start+=2;
									}else{
										atSource=0;
										// Read th cc we are mapping from
										n1=strtoul(start, &tail, 0);
										start=tail;

									}
									// Read the cc/rpn/nrpn we are mapping to (or range for pitch bend)
									while(*start==' ' || *start=='\t') start++;
									if (*start==',') start++; // optional comma separator
									n2=strtoul(start, &tail, 0);
									while(*tail==' ' || *tail=='\t') tail++;
									if (*tail==',') tail++; // accept trailing comma
									while(*tail==' ' || *tail=='\t') tail++;
									if (*tail && *tail != '#' && *tail != '\n'){
										errormessage("Error: invalid destination %s", start);
										exit(-1);
									}else{
										if(atSource){
											atType=currentType;
											atLSB=n2 & 0x7F;
											atMSB=n2 >> 7;
											// Should check range depending on type
											if(verbose)
												printf("aftertouch to type %u %s %s %u (0x%02x)\n", atType, mapNames[atType],
													(atType==PB)?"scale":"value", // FIXME shouldn't use map_to as scale
													n2, n2);
										}else{
											if (set_maps(currentType, n1, n2)){
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
					break;
				default:
					errormessage("Error: Unknown option %s", argv[i]);
					usage(argv[0]);
					exit(-1);
			}
		}else{
			if(need_map){
				n2=strtoul(argv[i], &tail, 0);
				if (*tail){
					errormessage("Error: invalid destination \"%s\"", argv[i]);
					exit(-1);
				}
				if (set_maps(currentType, n1, n2)){
					errormessage("Error: invalid mapping, aborting");
					exit(-1);
				}
				need_map=0;
			}else{
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
	
	/*
	// Process rest of command line parameters as space-separated CC NRPN pairs
	while(i+1<argc){
		int cc, nrpn;
		cc=atoi(argv[i]); // FIXME
		nrpn=atoi(argv[i+1]); // FIXME
		if(cc<0 || cc>127){
		 errormessage("Problem processing cc: %s", argv[i]);
		 exit(-1);
		}
		if(nrpn<0 || nrpn>16383){
		 errormessage("Problem processing nrpn: %s", argv[i+1]);
		 exit(-1);
		}
		mapType[cc]=NRPN;
		mapLSB[cc]=nrpn & 0x7F;
		mapMSB[cc]=nrpn >> 7;
		if(verbose)	printf("cc %u (0x%2x) to nrpn %u (0x%2x)\n", cc, cc, nrpn, nrpn);
		i+=2;
	}
	if (i<argc){
		errormessage("Ignoring unexpected trailing parameter: %s", argv[i]);
		// exit(-1);
	}
*/
	if ((status = snd_rawmidi_open(&midiin, &midiout, "virtual", mode)) < 0) {
		errormessage("Problem opening MIDI input: %s", snd_strerror(status));
		exit(1);
	}

	int max_count = 0;   // Exit after this many bytes have been received, 0 for no limit.
	int count = 0;         // Current count of bytes received.
	// int status_count = 0;         // Current count of status bytes received.
	int total_count = 0;
	// int data_count; // Number of data bytes following MIDI status byte
	char buffer[buf_size];        // Storage for input buffer received
	unsigned char msgCtrl[buf_size]; // cc 99, 98, 6, optionally 38, + reset rpn
	int k; // Index in msgCtrl
	// printf("\nEAGAIN: %d %s %s", EAGAIN, snd_strerror(-EAGAIN), snd_strerror(EAGAIN));
	// -> EAGAIN=11 Resource temporarily unavailable
	// fflush(stdout);
	// status = snd_rawmidi_read(midiin, &buffer[count], 1); // -11 on non-blocking, 1 on blocking
	// status = snd_rawmidi_read(midiin, &buffer[count], 3); // 3 on blocking
	// if(status>0) count=status;
	// printf("\n#read status %d %s\n", status, snd_strerror(status));
	// fflush(stdout);
	// for(int i=0; i<count; i++){
	//	   printf("%u ", (unsigned char)buffer[i]);
	// };
	readState = PASSTHRU;

	while (total_count<max_count || max_count<1) {
		/* Blocking version - don't use
		// blocking mode on "virtual" drops bytes ???
		while (count<buf_size && status == 1) {
			status = snd_rawmidi_read(midiin, &buffer[count++], 1);
		}
		printf("\n>read status: %d %s, count: %d\n", status, snd_strerror(status), count);
		*/
		
		// Non-blocking version
		count = 0;
		status = snd_rawmidi_read(midiin, buffer, buf_size);
		while (status == -EAGAIN) { // Keep polling
			usleep(320); // One physical MIDI byte
			status = snd_rawmidi_read(midiin, buffer, buf_size);
		}
		// snd_rawmidi_drain(midiin); // Stuck on C0 anyway ??
		// printf("\n>read status: %d %s, count: %d\n", status, status>0?"ok":snd_strerror(status), count);
		if (status>0){
			count = status; // Not pretty
			if(verbose>1) printf("\n[%u]", count);
		}else{
			errormessage("Problem reading MIDI input: %s", snd_strerror(status));
			break;
		}
		total_count+=count;
		fflush(stdout);	
		
		if(verbose>1) dump(buffer, count);

		for(int i=0; i<count; i++){
			if (buffer[i] & 0x80){ // Status byte, 80..FF
		        if(verbose>1) printf("S");
				runningStatus=buffer[i];
				channel = runningStatus & 0x0F;
				// data_count=data_length[(runningStatus & 0x70)>>4];
				// readState = ((runningStatus & 0xF0) == 0xB0) ? PROCESS_CC : PASSTHRU;
				switch(runningStatus & 0xF0) {
					case 0xB0:
						readState = PROCESS_CC;
						break;
					case 0xD0:
						readState = PROCESS_AT;
						break;
					default:
						readState = PASSTHRU;
				}
			}else{ // Data byte, 00..7F
		        if (verbose>1) printf("D");
		        switch (readState){
				case PROCESS_CC: // if (readState == PROCESS_CC){ // Got a cc number
					// Next state depends on map type
					if(verbose>1) printf("1");
					ccNum=buffer[i];
					if(mapType[ccNum] == NONE){ // No mapping
						// catch up with status
						if ((status = snd_rawmidi_write(midiout, &runningStatus, 1)) < 0) {
							errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
							break;
						};
						// Send unchanged cc
						if ((status = snd_rawmidi_write(midiout, &buffer[i], 1)) < 0) {
							errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
							break;
						};
						if(verbose>1) printf("s");
						readState = PROCESS_VAL;
					}else if(mapType[ccNum] == NRPN || mapType[ccNum] == RPN){
						readState = PROCESS_PARM;
						// Do nothing until value is received
					}else if(mapType[ccNum] == CC){
						// Catch up with status
						if ((status = snd_rawmidi_write(midiout, &runningStatus, 1)) < 0) {
							errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
							break;
						};
						if(verbose>1) printf("s");
						// Send remapped cc
						if ((status = snd_rawmidi_write(midiout, &mapMSB[ccNum], 1)) < 0) {
							errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
							break;
						};
						if(verbose>1) printf("c 0x%02x ", mapMSB[ccNum]);
						readState = PROCESS_VAL; // Will pass next byte (cc value) unchanged
						continue; // Done processing cc number
					}else if(mapType[ccNum] == PB){
						readState = PROCESS_PB;
					}else{
						errormessage("Internal error - unknown map type %u\n", mapType[ccNum]);
						exit(-1);
					}
					break;
				case PROCESS_PARM: // }else if (readState == PROCESS_PARM){ // RPN/NRPN mapping specific
					if(verbose>1) printf("2");
					ccVal=buffer[i];
					// see https://www.midi.org/specifications-old/item/table-3-control-change-messages-data-bytes-2
					k=0;
					msgCtrl[k++]=0xB0+channel;
					msgCtrl[k++]=(mapType[ccNum] == RPN)?0x65:0x63;
					msgCtrl[k++]=mapMSB[ccNum];
					// msgCtrl[3]=0xB0+channel; // Unneeded, unchanged running status
					msgCtrl[k++]=(mapType[ccNum] == RPN)?0x64:0x62;
					msgCtrl[k++]=mapLSB[ccNum];
					// msgCtrl[6]=0xB0+channel; // Unneeded, unchanged running status
					msgCtrl[k++]=0x06;
					msgCtrl[k++]=ccVal;
					msgCtrl[k++]=0x26;
					msgCtrl[k++]=0;
					// The following prevent accidental change of NRPN value
					msgCtrl[k++]=0x65; // RPN MSB
					msgCtrl[k++]=0x7F;
					msgCtrl[k++]=0x64; // RPN LSB
					msgCtrl[k++]=0x7F;
					// Luckily running status is still 0xBcc, no need to resend
					if(verbose>1) printf(" --> ");
					if ((status = snd_rawmidi_write(midiout, msgCtrl, k)) < 0) {
						errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
						exit(-1); // break;
					};
					if(verbose>1) dump(msgCtrl, k);

					readState = PROCESS_CC;
					break;
				case PROCESS_VAL: // }else if (readState == PROCESS_VAL){
				    // CC mapping specific, send value
					// Like passthru except a cc num can follow (running status is 0xB_ )
					if ((status = snd_rawmidi_write(midiout, &buffer[i], 1)) < 0) {
						errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
						exit(-1); // break;
					}
					readState = PROCESS_CC;
					break;
				case PROCESS_PB: // }else if (readState == PROCESS_PB){
				    // Pitch bend mapping specific, send scaled value
					if(verbose>1) printf("P");
					ccVal=buffer[i];
					scale=(mapMSB[ccNum]<<7) + mapLSB[ccNum]; // FIXME
					pbval=8192+(scale*ccVal)/127;
					k=0;
					msgCtrl[k++]=0xE0+channel;
					msgCtrl[k++]=pbval & 0x7F;
					msgCtrl[k++]=pbval >>7;
					// At this stage output running status is E0
					// while input running status is B0 (or D0)
					// We should echo the input running status
					// after sending then pitch bend
					// This is not always necessary depending on what follows
					// Sending it even when unneeded shouldn't harm
					// Todo: have a pending_status variable to resend only when needed
					msgCtrl[k++]=runningStatus;
					if ((status = snd_rawmidi_write(midiout, msgCtrl, k)) < 0) {
						errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
						exit(-1); // break;
					}
					readState = PROCESS_CC;
					break;
				case PROCESS_AT:
					if(verbose>1) printf("A");
					atVal=buffer[i];
					// TODO: Factor out
					// sendParm(midiout, channel, type, parmMSB, parmLSB, parmVal)
					// sendCc(midiout, channel, cc, value)
					// sendPb(midiout, channel, value, scale)
					k=0;
					switch (atType){
						case NONE:
							msgCtrl[k++]=runningStatus;
							msgCtrl[k++]=atVal;
							break;
						case CC:
						    msgCtrl[k++]=0xB0+channel;
						    msgCtrl[k++]=atMSB;
						    msgCtrl[k++]=atVal;
							break;
						case RPN:
						case NRPN:
							msgCtrl[k++]=0xB0+channel;
							msgCtrl[k++]=(atType == RPN)?0x65:0x63;
							msgCtrl[k++]=atMSB;
							msgCtrl[k++]=(atType == RPN)?0x64:0x62;
							msgCtrl[k++]=atLSB;
							msgCtrl[k++]=0x06;
							msgCtrl[k++]=atVal;
							msgCtrl[k++]=0x26;
							msgCtrl[k++]=0;
							// The following prevent accidental change of NRPN value
							msgCtrl[k++]=0x65; // RPN MSB
							msgCtrl[k++]=0x7F;
							msgCtrl[k++]=0x64; // RPN LSB
							msgCtrl[k++]=0x7F;
							break;
						case PB:
							scale=(atMSB<<7) + atLSB; // FIXME
							pbval=8192+(scale*atVal)/127;
							msgCtrl[k++]=0xE0+channel;
							msgCtrl[k++]=pbval & 0x7F;
							msgCtrl[k++]=pbval >>7;
							break;
						default:
							errormessage("Internal error - unknown aftertouch map type %u\n", atType);
							exit(-1);
					}
					// We don't need to resend status
					// next aftertouch message will always map to the same output status
					if ((status = snd_rawmidi_write(midiout, msgCtrl, k)) < 0) {
						errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
						exit(-1); // break;
					}
					readState=PROCESS_AT; // Handle input running status
					break;
				default:
					errormessage("Internal error - unexpected read state %u", readState);
					exit(-1);
				
				} // Switch readState
			}
			if (readState == PASSTHRU){
				if ((status = snd_rawmidi_write(midiout, &buffer[i], 1)) < 0) {
			        errormessage("Problem writing MIDI Output: %s", snd_strerror(status));
			        break;
		        }
		        if(verbose){
					printf(".");
					fflush(stdout);
				}
			}
		}

		count=0;
		/*
		if ((unsigned char)buffer[0] >= 0x80) {   // command byte: print in hex
		printf("\n%5u:0x%x ", count, (unsigned char)buffer[0]);
		status_count++;
		} else {
		printf("%d ", (unsigned char)buffer[0]);
		}
		fflush(stdout);
		if (count % 20 == 0) {  // print a newline to avoid line-wrapping
		 printf("\n   ");
		}
		*/ 
	}

	printf("\nTotal:%5u\n", total_count);

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


