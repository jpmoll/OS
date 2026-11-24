/*
 * chardev_comentado.c
 * Char device con read y write implementados (version comentada).
 * El codigo es el mismo que se recibio; solo se agregaron comentarios.
 */

/* Cabeceras: cada una trae las declaraciones que el modulo necesita */
#include <linux/kernel.h>    /* pr_info, pr_alert, min, scnprintf */
#include <linux/module.h>    /* macros de modulo: module_init, MODULE_LICENSE... */
#include <linux/fs.h>        /* struct file_operations, register_chrdev */
#include <linux/slab.h>      /* kmalloc y kfree (memoria dinamica del kernel) */
#include <linux/bitops.h>    /* test_and_set_bit, clear_bit (operaciones atomicas) */
#include <linux/uaccess.h>   /* get_user, copy_to/from_user (memoria de usuario) */

/* Metadatos del modulo (los puedes ver con: modinfo chardev.ko) */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Zair");
MODULE_DESCRIPTION("Char device con read y write implementados");

#define DEVICE_NAME "chardev"  /* nombre que aparece en /proc/devices */
#define BUF_LEN     80         /* tamano maximo del texto guardado */
#define BIT_ABIERTO 0          /* numero del bit de "estado" que indica "dispositivo ocupado" */

/* Variables globales del driver (static = solo visibles dentro de este archivo) */
static int           major;      /* major number que nos asigna el kernel */
static char         *texto;      /* buffer (en el kernel) donde se guarda el texto */
static size_t        texto_len;  /* cuantos bytes validos hay actualmente en "texto" */
static unsigned int  aperturas;  /* cuantas veces se ha abierto el dispositivo */
static unsigned long estado;     /* bit 0 = 1 si alguien lo tiene abierto */

/*
 * Se llama cuando un proceso hace open() sobre /dev/chardev.
 * Ejemplo: al ejecutar "cat /dev/chardev" o "echo hola > /dev/chardev",
 * la shell abre el archivo y el kernel llama a esta funcion.
 */
static int dev_open(struct inode *inode, struct file *filp)
{
	/*
	 * test_and_set_bit hace dos cosas en un solo paso atomico:
	 * pone el bit en 1 y devuelve el valor que tenia antes.
	 * Si ya valia 1, otro proceso lo tiene abierto -> devolvemos -EBUSY
	 * ("dispositivo ocupado"). Asi solo un proceso a la vez puede usarlo.
	 */
	if (test_and_set_bit(BIT_ABIERTO, &estado))
		return -EBUSY;

	aperturas++;
	pr_info("%s: abierto, apertura numero %u\n", DEVICE_NAME, aperturas);

	return 0; /* 0 = exito */
}

/*
 * Se llama cuando el proceso cierra el archivo (close).
 * Libera el "candado" para que otro proceso pueda abrirlo.
 */
static int dev_release(struct inode *inode, struct file *filp)
{
	clear_bit(BIT_ABIERTO, &estado); /* bit en 0 = dispositivo libre */
	pr_info("%s: cerrado\n", DEVICE_NAME);

	return 0;
}

/*
 * Se llama cuando un proceso hace read() (por ejemplo, "cat /dev/chardev").
 *   buffer : memoria del USUARIO donde hay que dejar los datos (por eso __user)
 *   length : cuantos bytes como maximo quiere leer el usuario
 *   offset : posicion actual de lectura dentro del "archivo"
 */
static ssize_t dev_read(struct file *filp, char __user *buffer,
			size_t length, loff_t *offset)
{
	ssize_t entregados;

	/*
	 * simple_read_from_buffer ya hace todo el trabajo:
	 *  - copia desde "texto" (kernel) hacia "buffer" (usuario) de forma segura
	 *  - respeta "length" y "texto_len"
	 *  - avanza *offset
	 *  - cuando ya se entrego todo, devuelve 0 (= fin de archivo),
	 *    y por eso cat termina en vez de leer para siempre
	 */
	entregados = simple_read_from_buffer(buffer, length, offset,
					     texto, texto_len);

	if (entregados > 0)
		pr_info("%s: entregados %zd bytes al usuario\n",
			DEVICE_NAME, entregados);

	return entregados; /* bytes entregados, 0 = EOF, negativo = error */
}

/*
 * Se llama cuando un proceso hace write().
 * ESTA es la parte que permite usar: echo "hola" > /dev/chardev
 *   buff : memoria del USUARIO con los datos a guardar (const: solo se lee)
 *   len  : cuantos bytes quiere escribir el usuario
 */
static ssize_t dev_write(struct file *filp, const char __user *buff,
			 size_t len, loff_t *off)
{
	size_t i = 0;
	/* Maximo a guardar: lo menor entre lo que envian y BUF_LEN-1 (se deja 1 byte para '\0') */
	size_t tope = min(len, (size_t)(BUF_LEN - 1));
	char c;

	/* Copia byte a byte desde la memoria del usuario hacia "texto" (kernel) */
	while (i < tope) {
		/*
		 * get_user lee 1 byte de memoria de usuario de forma segura.
		 * Si el puntero es invalido devuelve distinto de 0 -> -EFAULT.
		 * (Es el "hermano" de put_user, pero en sentido contrario.)
		 */
		if (get_user(c, buff + i))
			return -EFAULT;

		texto[i] = c;
		i++;
	}

	texto[i] = '\0';  /* cierra el texto como cadena C */
	texto_len = i;    /* actualiza cuantos bytes validos hay (lo usa dev_read) */

	pr_info("%s: recibidos %zu bytes del usuario\n", DEVICE_NAME, i);

	/*
	 * Se devuelve "len" (todo lo que envio el usuario), no "i".
	 * Asi echo cree que se consumio todo y no vuelve a llamar a write.
	 * Consecuencia: si enviaron mas de 79 bytes, el exceso se pierde en silencio.
	 */
	return len;
}

/*
 * Tabla de operaciones: le dice al kernel que funcion llamar
 * para cada operacion sobre el dispositivo. Los campos no asignados quedan en NULL.
 */
static const struct file_operations fops = {
	.owner   = THIS_MODULE, /* el kernel lleva la cuenta de uso del modulo (evita rmmod si esta en uso) */
	.read    = dev_read,
	.write   = dev_write,
	.open    = dev_open,
	.release = dev_release,
};

/*
 * Se ejecuta al cargar el modulo (sudo insmod chardev.ko).
 * __init: el kernel libera esta funcion de memoria despues de ejecutarla.
 */
static int __init chardev_init(void)
{
	/* Pide 80 bytes de memoria al kernel. GFP_KERNEL = se permite esperar si hace falta. */
	texto = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!texto)
		return -ENOMEM; /* sin memoria: el modulo no se carga */

	/* Mensaje inicial: es lo que muestra cat antes de que alguien escriba */
	texto_len = scnprintf(texto, BUF_LEN,
			      "Todavia nadie me ha escrito nada.\n");

	/* major = 0 pide un major number dinamico; el kernel devuelve el asignado */
	major = register_chrdev(0, DEVICE_NAME, &fops);
	if (major < 0) {
		kfree(texto); /* si falla, liberar lo que ya se pidio */
		pr_alert("%s: fallo al registrar el dispositivo: %d\n",
			 DEVICE_NAME, major);
		return major;
	}

	/* Instrucciones para el usuario: se ven con "sudo dmesg | tail" */
	pr_info("%s: registrado con major number %d\n", DEVICE_NAME, major);
	pr_info("%s: cree el archivo con 'sudo mknod /dev/%s c %d 0'\n",
		DEVICE_NAME, DEVICE_NAME, major);
	pr_info("%s: y luego 'sudo chmod 666 /dev/%s'\n",
		DEVICE_NAME, DEVICE_NAME);

	return 0;
}

/*
 * Se ejecuta al descargar el modulo (sudo rmmod chardev).
 * __exit: esta funcion solo existe para modulos que se pueden descargar.
 */
static void __exit chardev_exit(void)
{
	unregister_chrdev(major, DEVICE_NAME); /* devuelve el major al kernel */
	kfree(texto);                          /* libera el buffer: el texto guardado se pierde */
	pr_info("%s: modulo retirado (borre /dev/%s con rm)\n",
		DEVICE_NAME, DEVICE_NAME);
}

/* Indican al kernel cual es la funcion de entrada y cual la de salida del modulo */
module_init(chardev_init);
module_exit(chardev_exit);
