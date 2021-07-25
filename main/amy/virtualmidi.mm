// virtualmidi.m
/* -*- Mode: ObjC; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; fill-column: 150 -*- */

#import <array>
#import <cassert>
#import <cstdio>

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMidi/CoreMidi.h>


static CGEventSourceRef eventSource;
static std::array<unsigned char, 16*128> control;

static void NotifyProc(const MIDINotification *message, void *refCon)
{
}

int main(int argc, const char * argv[]) {
  @autoreleasepool {
    MIDIClientRef midi_client;
    OSStatus status = MIDIClientCreate((__bridge CFStringRef)@"VirtualAlles", NotifyProc, nullptr, &midi_client);
    if (status != noErr) {
      fprintf(stderr, "Error %d while setting up handlers\n", status);
      return 1;
    }
    printf("OK\n");
    eventSource = CGEventSourceCreate(kCGEventSourceStatePrivate);

    control.fill(0xFF);

    ItemCount number_sources = MIDIGetNumberOfSources();
    for (int i = 0; i < number_sources; i++) {
      MIDIEndpointRef source = MIDIGetSource(i);
      MIDIPortRef port;
      status = MIDIInputPortCreateWithProtocol(midi_client,
                                               (__bridge CFStringRef)[NSString stringWithFormat:@"VirtualAlles input %d", i],
                                               kMIDIProtocol_1_0,
                                               &port,
                                               ^(const MIDIEventList *evtlist, void *srcConnRefCon) {
                                                 const MIDIEventPacket* packet = &evtlist->packet[0];

                                                 for (int i = 0; i < evtlist->numPackets; i++) {
                                                   // We expect just MIDI 1.0 packets.
                                                   // The words are in big-endian format.
                                                   assert(packet->wordCount == 1);

                                                   const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&packet->words[0]);
                                                   assert(bytes[3] == 0x20);

                                                   printf("Event: %02X %02X %02X\n", bytes[2], bytes[1], bytes[0]);

                                                   switch ((bytes[2] & 0xF0) >> 4) {
                                                   case 0x9: // Note-On
                                                     assert(bytes[1] <= 0x7F);
                                                     printf("note on %d %d\n", (bytes[2] & 0x0F) * 128 + bytes[1], bytes[0]);
                                                     break;

                                                   case 0x8: // Note-Off
                                                     assert(bytes[1] <= 0x7F);
                                                     printf("note off %d %d\n", (bytes[2] & 0x0F) * 128 + bytes[1], bytes[0]);
                                                     break;

                                                   case 0xB: // Control Change
                                                     assert(bytes[1] <= 0x7F);
                                                     const int number = (bytes[2] & 0x0F) * 128 + bytes[1];
                                                     if (control.at(number) != 0xFF) {
                                                       int diff = bytes[0] - control.at(number);

                                                       // If it switches from 0 to 127 or back, we assume it is not really a continuous controller but
                                                       // a button.

                                                       if (diff == 127)
                                                         diff = 1;
                                                       else if (diff == -127)
                                                         diff = -1;

                                                       if (diff > 0) { 
                                                         for (int i = 0; i < diff; i++) {
                                                           // Send keys indicating single-step control value increase
                                                           printf("CC %d %d\n", 16*128 + number * 2, diff );
                                                         }
                                                       } else if (diff < 0) {
                                                         for (int i = 0; i < -diff; i++) {
                                                           // Send key indicating single-step control value decrease
                                                           printf("CC %d %d\n", 16*128 + number * 2 +1, -diff );
                                                         }
                                                       }
                                                     }
                                                     control.at(number) = bytes[0];
                                                     break;
                                                   }

                                                   packet = MIDIEventPacketNext(packet);
                                                 }
                                               });
      if (status != noErr) {
        fprintf(stderr, "Error %d while setting up port\n", status);
        return 1;
      }
      status = MIDIPortConnectSource(port, source, nullptr);
      if (status != noErr) {
        fprintf(stderr, "Error %d while connecting port to source\n", status);
        return 1;
      }
    }
    CFRunLoopRun();
  }
  return 0;
}