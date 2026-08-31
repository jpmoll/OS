#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <unistd.h>

#define MAXSIZE 128

struct msgbuf {
    long mtype;
    char mtext[MAXSIZE];
};

void die(char *s) {
    perror(s);
    exit(1);
}

int main(void) {
    int msqid;
    key_t key = 1234;
    struct msgbuf rcvbuffer;
    int msgflg = IPC_CREAT | 0666;

    if ((msqid = msgget(key, msgflg)) < 0)
        die("msgget");

    printf("====================================\n");
    printf("   LABORATORIO 02 - PROCESO CONSUMIDOR\n");
    printf("====================================\n");
    printf("Esperando mensajes en la cola IPC (key=%d)...\n\n", key);

    while (1) {
        /* mtype = 0 -> recibe el primer mensaje disponible, respetando
           el orden real de llegada a la cola (FIFO), sin importar el tipo */
        if (msgrcv(msqid, &rcvbuffer, MAXSIZE, 0, 0) < 0)
            die("msgrcv");

        printf("[ Mensaje recibido ]\n");
        printf("Tipo: %ld\n", rcvbuffer.mtype);

        switch (rcvbuffer.mtype) {
            case 1:
                printf("Procesando mensaje tipo 1: %s\n", rcvbuffer.mtext);
                break;
            case 2:
                printf("Procesando mensaje tipo 2: %s\n", rcvbuffer.mtext);
                break;
            case 3:
                printf("Procesando mensaje tipo 3: %s\n", rcvbuffer.mtext);
                break;
            default:
                printf("Tipo de mensaje desconocido: %s\n", rcvbuffer.mtext);
        }

        printf("Esperando 5 segundos...\n\n");
        sleep(5);
    }

    return 0;
}
