#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "utils/state_machine.h"
#include "connection.h"
#include "flags.h"

static struct termios oldtio;

int llread(int fd, char *buffer) {
    int bytes_read = 0, accept = 0, res = 0, alarm_count = MAX_ALARM_COUNT;

    do {
        alarm(TIMEOUT);
        res = read(fd, &buffer[bytes_read], sizeof(char));
        if (res == ERROR) {
            if (errno == EINTR) {
                --alarm_count;
                printf("read timeout %d\n", 3-alarm_count);
                continue;
            }

            perror("read failed");
            return ERROR;
        }
        
        printf("read hex: 0x%x ascii:%u\n", buffer[bytes_read], buffer[bytes_read]);
        accept = state_machine(buffer[bytes_read]);
        bytes_read++;

    } while (!accept && alarm_count > 0);
    alarm(0);

    if (alarm_count <= 0) {
        printf("Alarm limit reached.\n");
        return ERROR;
    }
    printf("read %d bytes\n", bytes_read);

    return bytes_read;
}

int llwrite(int fd, char *buffer, int length) {
    int bytes_written = 0, res;

    for (; bytes_written < length; ++bytes_written) {
        res = write(fd, &buffer[bytes_written], sizeof(char));
        if (res == ERROR) {
            perror("write error");
            return ERROR;
        }

        printf("wrote hex: 0x%x ascii:%u\n", buffer[bytes_written], buffer[bytes_written]);
    }

    printf("wrote %d bytes\n", bytes_written);

    return bytes_written;
}

int calc_bcc2(char *packet, size_t length){
    int bcc2 = BCC(packet[0], packet[1]);

    for(int i = 2; i < length; ++i){
        bcc2 = BCC(bcc2, packet[i]); 
    }
    return bcc2;
}

char *build_packet(char *fragment, size_t *length) {
    static char seq_no = 0;

    *length += PACKET_HEAD_LEN + 1;

    char *packet = malloc((*length) * sizeof(char));
    sprintf(packet, "%c%c%c%c", DATA_PACKET, seq_no++, (*length) / 255, (*length) % 255);
    memcpy(&packet[4], fragment, *length);

    return packet;
}

char *build_control_packet(file_t *file_info, size_t *length){
    printf("new length: %d\n", *length);
    size_t filename_len = strlen(file_info->filename);
    
    *length = 5 + filename_len + 8 + 1;
    char *ctrl_packet = malloc(*length);


    sprintf(ctrl_packet, "%c%c%c%ld%c%c", file_info->ctrl, FILE_SIZE_PARAM, 8, file_info->file_size, FILE_NAME_PARAM, strlen(file_info->filename));
    memcpy(&ctrl_packet[6 + 8], file_info->filename, filename_len);

    for (int i = 0; i < *length; ++i)
        printf("CTRL: 0x%x\n", ctrl_packet[i]);

    return ctrl_packet;
}

char *build_frame(frame_t *frame){
    char *packet;
    printf("building control packet with initial length %d\n", frame->length);
    switch (frame->request_type) {
        case DATA_REQ:
            packet = build_packet(frame->packet->fragment, &(frame->length));
            break;
        case CTRL_REQ:
            packet = build_control_packet(frame->file_info, &(frame->length));
        default:break;
    }

    for (int i = 0 ; i < frame->length; ++i) {
        printf("packet[%d] = 0x%02x\n", i, packet[i]);
    }
    printf("control packet built\n");


    int bcc2 = calc_bcc2(packet, frame->length);

    char *frame_str = malloc(frame->length + FRAME_I_LENGTH + 1);
    sprintf(frame_str, "%c%c%c%c", FLAG, frame->packet->addr, frame->packet->ctrl, BCC(frame->packet->addr, frame->packet->ctrl));
    memcpy(&frame_str[4], packet, frame->length);
    sprintf(&frame_str[4 + (frame->length)], "%c%c", bcc2, FLAG);

    return frame_str;
}

int send_packet(int fd, frame_t *frame){
    char *frame_str = build_frame(frame);
    
    printf("FRAME: %s\n", frame_str);
    
    return llwrite(fd, frame_str, frame->length);
}

int check_cmd(int fd, char cmd_byte, char *cmd) {
    int bytes_read = 0; 
    while (cmd[2] != cmd_byte) {
        bytes_read = llread(fd, cmd);
        if (bytes_read == ERROR) {
            exit(ERROR);
        }
    }

    return bytes_read;
}

int send_set(int fd) {
    char set_command[TYPE_A_PACKET_LENGTH + 1];
    int bcc = BCC(SENDER_CMD, SET_CMD);

    sprintf(set_command, "%c%c%c%c%c", FLAG, SENDER_CMD, SET_CMD, bcc, FLAG);

    int bytes_written = llwrite(fd, set_command, TYPE_A_PACKET_LENGTH);

    char ack_command[TYPE_A_PACKET_LENGTH + 1];
    bzero(ack_command, TYPE_A_PACKET_LENGTH + 1);

    int bytes_read = check_cmd(fd, UACK_CMD, ack_command);

    return bytes_read;
}

int send_ack(int fd) {
    char set_command[TYPE_A_PACKET_LENGTH + 1];
    bzero(set_command, TYPE_A_PACKET_LENGTH + 1);

    int bytes_read = check_cmd(fd, SET_CMD, set_command);

    char ack_command[TYPE_A_PACKET_LENGTH + 1];
    int bcc = BCC(RECEIVER_ANS, UACK_CMD);
    
    sprintf(ack_command, "%c%c%c%c%c", FLAG, RECEIVER_ANS, UACK_CMD, bcc, FLAG);

    int bytes_written = llwrite(fd, ack_command, TYPE_A_PACKET_LENGTH);
    return bytes_written;
}

void terminal_setup(int fd) {
    struct termios newtio;

    int tc_attr_status = tcgetattr(fd, &oldtio);
    if (tc_attr_status == ERROR)
    {
        /* save current port settings */
        perror("tcgetattr error");
        exit(ERROR);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 0; /* inter-character timer unused */
    newtio.c_cc[VMIN] = 5;  /* blocking read until 5 chars received */

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == ERROR)
    {
        perror("tcsetattr error");
        exit(ERROR);
    }

    printf("New termios structure set\n");
}

int llopen(int port, int mode) {
    char device[10];

    sprintf(device, "/dev/ttyS%d", port);
    puts(device);

    int fd = open(device, O_RDWR | O_NOCTTY);
    if (fd == ERROR) {
        perror(device);
        return fd;
    }

    terminal_setup(fd);

    sender_func functions[] = {send_set, send_ack};
    functions[mode](fd);

    return fd;
}
