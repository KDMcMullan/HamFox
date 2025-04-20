# HamFox
An Arduino program to transmit a message at a regular interval in order that the device can be found by other radio amateurs.

## History

This is basically a subset of a program I wrote a while ago (https://github.com/KDMcMullan/HaMQTT) to receive DTMF sequences from a Ham radio, and retransmit those as MQTT packets. One of the incidental features of same was the ability to periodically transmit the owner's callsign. It's that feature which has been isolted here. This is a standalone program to broadcast a voice message periodically, such that the transmitter can be sought, by qppropriately equipped individuals, as part of a ham radio "fox hunt". 

Taking advantage of another core function of the predecessor, this "fox" can be remotely controlled by sending it DTMF sequences. 

Initially, the arduino will braodcast in the voice of a 1980's Speak 'n' Spell speech synthesiser, thanks to https://github.com/earlephilhower/ESP8266Audio but the library supports the playing of sampled audio files: an obvious evolution.

v0.90.01 20250420
First pass. Removed all the uneccessary code for MQTT (which I'll probably regret), added the necessities for regular retransmit, and a few of the basic DTMD commands. Tested the regular transmit. The DTMF receive code is not tested, but I think it's a hardware fault. 

## To Do

Periodically transmit a message. Suggestion is "This is <callsign> slash Foxtrot Hotel. This frequency is in use for a fox hunt" x2. This would be followed by a number of repititions (8x) of "Find the fox" to make the total transmit time up to about 30 seconds. The message would terminate with "Over to you."

Prposed controllable features are:
 - count of repetitions of the secondary message
 - the total time (in seconds) between the start of each broadcast
 - zero for "off"
 
Other proposed commands:
 - command to query the above.
 - command to instigae one of a number of other

