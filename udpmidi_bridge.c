#include <alsa/asoundlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>

int main() {
    snd_seq_t *seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) return 1;
    snd_seq_set_client_name(seq, "Pico MIDI");

    int port;
    snd_seq_create_simple_port(seq, "Pico MIDI",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    snd_midi_event_t *enc;
    snd_midi_event_new(64, &enc);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    uint8_t buf[64];
    while (1) {
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) continue;
            printf("got %d bytes: %02x %02x %02x\n", (int)n, buf[0], buf[1], buf[2]);      
        if (n <= 0) continue;

        snd_seq_event_t ev;
        long consumed = snd_midi_event_encode(enc, buf, n, &ev);
        if (consumed <= 0) continue;  // invalid MIDI, skip

        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_event_output(seq, &ev);
        snd_seq_drain_output(seq);
    }
    return 0;
}   




/*#include <alsa/asoundlib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>

int main() {
    snd_seq_t *seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        fprintf(stderr, "seq open failed\n");
        return 1;
    }
    snd_seq_set_client_name(seq, "Pico MIDI");

    int port;
    if (snd_seq_create_simple_port(seq, "Pico MIDI",
            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION) < 0) {
        fprintf(stderr, "port create failed\n");
        return 1;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    uint8_t buf[64];
    while (1) {
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) continue;

        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, port);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_ev_set_variable(&ev, n, buf);

        snd_seq_event_output(seq, &ev);
        snd_seq_drain_output(seq);
    }
    return 0;
}   
*/
