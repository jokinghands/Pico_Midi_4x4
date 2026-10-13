Simple Midi controller over USB.
Using RP Pico2040 in Arduino IDE

On Linux you can use the UDP Packets directly on the Midi-line(Pipewire/Jack/Alsa)
udpmidi_bridge.c is attached, you can compile it with:

gcc udpmidi_bridge.c -o udpmidi_bridge -lasound

#then start the process in a terminal(opened in the same folder):

./udpmidi_bridge 

or try the attached udpmidi_bridge in the Folder



<img width="800" height="800" alt="midi4x4" src="https://github.com/user-attachments/assets/a54cfb5d-f113-4717-993e-9db32bdfc92b" />
