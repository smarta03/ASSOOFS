//IMPLEMENTAR PROGRAMA QUE PERMITA FORMATEAR DISPOSITIVOS EN DE BLOQUES COMO ASSOOFS

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "assoofs.h"

#define WELCOMEFILE_DATABLOCK_NUMBER (ASSOOFS_LAST_RESERVED_BLOCK + 1)
#define WELCOMEFILE_INODE_NUMBER (ASSOOFS_LAST_RESERVED_INODE + 1)

//Inicializacion estatica de una estructura
static int write_superblock(int fd) {
    struct assoofs_super_block_info sb = {
        .version = 1,
        .magic = ASSOOFS_MAGIC,
        .block_size = ASSOOFS_DEFAULT_BLOCK_SIZE,
        .inodes_count = WELCOMEFILE_INODE_NUMBER,   //Definido al principio, para formatear y
                                                    //y que meta directamente un archivo, seria
                                                    //el ultimo inodo reservado +1
        .free_blocks = (~0) & ~(15),    //Pone todos lo bloques a 0 menos los 4 primeros.
    };                                  //Genera el mapa de bits.
                                        //
    ssize_t ret;

    //Escribe dentro del dispositvo el superbloque
    ret = write(fd, &sb, sizeof(sb));
    if (ret != ASSOOFS_DEFAULT_BLOCK_SIZE) {    //Si no coincide devuelve -1
        printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
        return -1;
    }

    printf("Super block written succesfully.\n");   //Todo correcto
    return 0;
}

//Guarda el inodo de directorio raiz en el amacen de inodos
static int write_root_inode(int fd) {
    ssize_t ret;

    struct assoofs_inode_info root_inode;

    root_inode.mode = S_IFDIR;  //Flag que especifica un directorio
    root_inode.inode_no = ASSOOFS_ROOTDIR_INODE_NUMBER; //Especificado en la estructura
    root_inode.data_block_number = ASSOOFS_ROOTDIR_BLOCK_NUMBER;
    root_inode.dir_children_count = 1;  //Se define directorio (union de la estructura)

    ret = write(fd, &root_inode, sizeof(root_inode));

    if (ret != sizeof(root_inode)) {    //Error
        printf("The inode store was not written properly.\n");
        return -1;
    }

    printf("root directory inode written succesfully.\n");  //Todo ok
    return 0;
}

//Guarfa el inodo del fichero README.txt en el almacen de inodos
static int write_welcome_inode(int fd, const struct assoofs_inode_info *i) { //assoofs_inode_info estructura del fichero de inicio
    off_t nbytes;
    ssize_t ret;

    ret = write(fd, i, sizeof(*i)); //escribe la estructura del fichero en el sistema de ficheros
    if (ret != sizeof(*i)) {
        printf("The welcomefile inode was not written properly.\n");
        return -1;
    }
    printf("welcomefile inode written succesfully.\n");

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - (sizeof(*i) * 2); //Actualizar el puntero interno a despues del fichero
                                                            //*2 porque hay dos estructuras ya cargadas
    ret = lseek(fd, nbytes, SEEK_CUR);  //Avanza el puntero, SEEK_CUR no importante
    if (ret == (off_t)-1) {
        printf("The padding bytes are not written properly.\n");
        return -1;
    }

    printf("inode store padding bytes (after two inodes) written sucessfully.\n");
    return 0;
}

//Guarda una entrada <nombre,numero de inodo> para el fichero README.txt en el bloque que
//almacena las entradas del directorio raiz
int write_dirent(int fd, const struct assoofs_dir_record_entry *record) {
    ssize_t nbytes = sizeof(*record), ret;

    ret = write(fd, record, nbytes);    //Escribe dentro del dispositivo el record
    if (ret != nbytes) {
        printf("Writing the rootdirectory datablock (name+inode_no pair for welcomefile) has failed.\n");
        return -1;
    }
    printf("root directory datablocks (name+inode_no pair for welcomefile) written succesfully.\n");

    nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - sizeof(*record);  //Calcula el avance del puntero
    ret = lseek(fd, nbytes, SEEK_CUR);  //Avanza el putero
    if (ret == (off_t)-1) {
        printf("Writing the padding for rootdirectory children datablock has failed.\n");
        return -1;
    }
    printf("Padding after the rootdirectory children written succesfully.\n");
    return 0;
}

//Escribe un mensaje en el bloque que almacena los contenidos del fichero README.txt
int write_block(int fd, char *block, size_t len) {
    ssize_t ret;

    ret = write(fd, block, len);    //Escribier en el fichero el bloque
    if (ret != len) {
        printf("Writing file body has failed.\n");
        return -1;
    }
    printf("block has been written succesfully.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    //Codigo para generar el documento incial de bienvenida
    //Descriptor del fichero
    int fd;
    ssize_t ret;
    char welcomefile_body[] = "Hola mundo, os saludo desde un sistema de ficheros ASSOOFS.\n";
    
    //Estructuras del documento de bievenida
    struct assoofs_inode_info welcome = {
        .mode = S_IFREG,    //Flag de archivo
        .inode_no = WELCOMEFILE_INODE_NUMBER,   //Definido al principio
        .data_block_number = WELCOMEFILE_DATABLOCK_NUMBER,  //Definido al principio
        .file_size = sizeof(welcomefile_body),  //Tamannio de la cadena anterior "Hola mundo, ..."
	.remove_flag = NO_REMOVED,
    };
    
    struct assoofs_dir_record_entry record = {
        .filename = "README.txt", //Nombre del archivo dentro del directorio
        .inode_no = WELCOMEFILE_INODE_NUMBER,
	.remove_flag = NO_REMOVED,
    };

    if (argc != 2) {    //No se le pasa dispositivo (USB, imagen ISO,...) Error
        printf("Usage: mkassoofs <device>\n"); 
        return -1;
    }

    //fd = descriptor del fichero
    fd = open(argv[1], O_RDWR); //El dispositivo fd se abre igual que un fichero
    if (fd == -1) { //Si es -1 Error
        perror("Error opening the device");
        return -1;
    }   //Sino

    ret = 1;
    do {
        if (write_superblock(fd)) //Escribe el superbloque en el bloque 0
            break;

        if (write_root_inode(fd))   //Guarda el inodo de directorio raiz en el amacen de inodos
            break;
        
        if (write_welcome_inode(fd, &welcome))  //Guarfa el inodo del fichero README.txt en el almacen de inodos
            break;

        if (write_dirent(fd, &record))  //Guarda una entrada <nombre,numero de inodo> para el fichero README.txt en el bloque que
                                        //almacena las entradas del directorio raiz
            break;
        
        if (write_block(fd, welcomefile_body, welcome.file_size))   //Escribe un mensaje en el bloque que almacena los contenidos
                                                                    //del fichero README.txt
            break;

        ret = 0;
    } while (0);

    close(fd);
    return ret;
}
