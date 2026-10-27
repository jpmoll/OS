#!/bin/bash
# Borra los recursos IPC que haya dejado shm.c si algun programa
# se cerro a la mala (Ctrl+C). ipcs lista, ipcrm elimina.
ipcrm -M 5678 2>/dev/null     # memoria compartida con clave 5678
ipcrm -S 0x1111 2>/dev/null   # semaforos con clave 0x1111
echo "Recursos IPC limpiados"
ipcs
