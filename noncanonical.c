/*Non-Canonical Input Processing*/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "application.h"

#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1

volatile int STOP = FALSE;

void alarm_handler() { printf("Received alarm.\n"); }

void register_signal_handler() {
  struct sigaction action;
  action.sa_handler = alarm_handler;
  sigaction(SIGALRM, &action, NULL);
}

int main(int argc, char **argv) {
  int mode;

  if(strcmp("receiver", argv[2]) == 0)
    mode = 1;
  else if(strcmp("sender", argv[2]) == 0)
    mode = 0;
  else {
    mode = -1;
  }

  if ((argc < 2) ||
        ((strcmp("/dev/ttyS0", argv[1])!=0) &&
        (strcmp("/dev/ttyS1", argv[1])!=0) &&
        (strcmp("/dev/ttyS4", argv[1])!=0)) || (mode == -1)) {
    printf("Usage:\tnserial SerialPort mode filename (opt)\n\tex: nserial /dev/ttyS1 sender pinguim.gif\n\tex: nserial /dev/ttyS1 receiver\n");
    exit(1);
  }

  int port = atoi(&argv[1][9]);

  register_signal_handler();  

  char *filename;
  if(argc == 4){
    filename = malloc(strlen(argv[3]) + 1);
    strcpy(filename, argv[3]);
  }
  else filename = NULL;

  start_app(port, mode, filename);

  func_ptr functions[] = {send_file, receive_file};
  functions[mode](filename);

  free(filename);


  // Reenvio

  /*
    O ciclo WHILE deve ser alterado de modo a respeitar o indicado no gui�o
  */

  return 0;
}
