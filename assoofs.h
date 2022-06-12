//DECLARACION DE ESTRUCTURAS DE DATOS Y CONSTANTES

#define ASSOOFS_MAGIC 0x20200406    //Identificar al dispositivo (es aleatorio)
#define ASSOOFS_DEFAULT_BLOCK_SIZE 4096 //Tamannio del bloque
#define ASSOOFS_FILENAME_MAXLEN 255     //Longitud maxima del nombre de un fichero 255 caracteres
#define ASSOOFS_LAST_RESERVED_BLOCK ASSOOFS_ROOTDIR_BLOCK_NUMBER    //Ultimo bloque reservado
#define ASSOOFS_LAST_RESERVED_INODE ASSOOFS_ROOTDIR_INODE_NUMBER    //Ultimo inodo reservado
//Flag para eliminar   
#define REMOVED 1
#define NO_REMOVED 0
const int ASSOOFS_SUPERBLOCK_BLOCK_NUMBER = 0;  //Bloque donde esta el SUPERBLOQUE
const int ASSOOFS_INODESTORE_BLOCK_NUMBER = 1;  //Numero de bloque donde se almacenan lo inodos
const int ASSOOFS_ROOTDIR_BLOCK_NUMBER = 2;     
const int ASSOOFS_ROOTDIR_INODE_NUMBER = 1;
const int ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED = 64;    //Numero maximo de ficheros o carpetas

//Estructura para el supebloque
struct assoofs_super_block_info {
    uint64_t version;
    uint64_t magic;
    uint64_t block_size;    
    uint64_t inodes_count;  //1 libre 0 ocupado
    uint64_t free_blocks;   //entero 64 bits
    //Hasta aqui 40 bytes
    //Se para dejar un hueco hasta 4096 (4096-5variablesAnteriores(x8bytes))=4056
    char padding[4056];
};

//Identificar los directorios y lo que hay dentro
struct assoofs_dir_record_entry {
    char filename[ASSOOFS_FILENAME_MAXLEN]; //Nombre del archivo
    uint64_t inode_no;  //Inodo
    uint64_t remove_flag; 
};

//Informacion de los inodos
struct assoofs_inode_info {
    mode_t mode;                //Permisos
    uint64_t inode_no;          //Numero de inodo
    uint64_t data_block_number; //Numero de bloque
    uint64_t remove_flag;       
    union {
        uint64_t file_size;             //Si es un archivo usa esta (tamannio archivo)
        uint64_t dir_children_count;    //Si es un directorio usa esta (numero de archivos dentro)
    };
};
