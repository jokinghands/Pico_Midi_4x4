Simple Midi controller over USB.
Using RP Pico2040 in Arduino IDE

On Linux you can use the UDP Packets directly on the Midi-line(Pipewire/Jack/Alsa)
try the attached udpmidi_bridge in this repository or 
use udpmidi_bridge.c and compile it with:

gcc udpmidi_bridge.c -o udpmidi_bridge -lasound

#start the process in a terminal(opened in the same folder):

./udpmidi_bridge 

# to see if the daemon connected to alsa

aconnect -l

# or use any other way to collect udp from air

// you could check it on linux with one of those:
//sudo tcpdump -i any -nn udp port 8080 -A
//nc -ul 8080
//nc -ul 8080 | xxd
//nc -ul 8080 | xxd -c 3




<img width="800" height="800" alt="midi4x4" src="https://github.com/user-attachments/assets/a54cfb5d-f113-4717-993e-9db32bdfc92b" />
