// IMPLEMENTAR MODULO PARA QUE EL KERNEL DEL SO PUEDA INTERACTUAR
// CON UN DISPOSITIVO DE BLOQUES DE ASSOOFS

#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_INFO  */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/fs.h>          /* libfs stuff           */
#include <linux/buffer_head.h> /* buffer_head           */
#include <linux/slab.h>        /* kmem_cache            */
#include "assoofs.h"

MODULE_LICENSE("GPL");

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);
ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct buffer_head *bh;
    char *buffer;
    int nbytes;

    // Obtener la informacion persistente del inodo a partir de filp
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;

    printk(KERN_INFO "Read request\n");

    // Comprobar si el valor del pos ha llegado al final del fichero
    if (*ppos >= inode_info->file_size)
        return 0;

    // Acceso al contenido del fichero almacenandolo en el buffer
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
    buffer = (char *)bh->b_data;

    // Copiar el contenido del buffer a buf, la operacion copy_to_user copia la informacion entre
    // kernel y usuario
    nbytes = min((size_t)inode_info->file_size, len); // Hay que comparar len con el tama~no del fichero por si llegamos al
                                                      // final del fichero
    if(copy_to_user(buf, buffer, nbytes)!=0){
        printk(KERN_INFO "Read ERROR\n");
    }

    // Incrementar el valor de ppos
    *ppos += nbytes;
    return nbytes;
}

ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{

    struct buffer_head *bh;
    char *buffer;
    
    // Obtener la informacion persistente del inodo a partir de filp
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    
    printk(KERN_INFO "Write request\n");

    // Acceso al contenido del fichero
    bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);

    // Escribir en el fichero los datos obtenidos de buf
    buffer = (char *)bh->b_data;
    buffer += *ppos;
    if(copy_from_user(buffer, buf, len)!=0){
        printk(KERN_INFO "Write ERROR\n");
    }

    // Incrementar el valor de ppos, marcar el bloque como sucio y sincronizar
    *ppos += len;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    // Actualizar el campo file_size de la información persistente del inodo y devolver el numero
    // de bytes escritos

    inode_info->file_size = *ppos;
    //filp->f_path.dentry->d_inode->i_sb superbloque
    assoofs_save_inode_info(filp->f_path.dentry->d_inode->i_sb, inode_info);
    return len;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx)
{
    struct inode *inode;
    struct super_block *sb;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i=0;

    printk(KERN_INFO "Iterate request\n");

    // Acceder al inodo, informacion persistente del inodo y superbloque correspondientes del contexto
    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    inode_info = inode->i_private;

    // Comprueba si el contexto del directorio ya estaba creado
    if (ctx->pos) return 0;

    // Comprobar que el inodo del primer paso corresponde con el contexto
    if ((!S_ISDIR(inode_info->mode))) return -1;

    // Accedemos al bloque donde se almacena el contenido del directorio y con su informacion
    // inicializamos el contexto
    
    bh = sb_bread(sb, inode_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++)
    {
        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);
        ctx->pos += sizeof(struct assoofs_dir_record_entry);
        record++;
    }
    brelse(bh);

    // Todo ha ido bien
    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode);
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

/*
 * Obtener la información persistente del inodo del superbloque
 */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no)
{

    struct assoofs_inode_info *inode_info = NULL;
    struct buffer_head *bh;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    for (i = 0; i < afs_sb->inodes_count; i++)
    {
        if (inode_info->inode_no == inode_no)
        {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;
    }

    brelse(bh);
    return buffer;
}

/*
 * Funcion auxiliar: obtiene un puntero con el inodo numero ino del superbloque sb
 */

static struct inode *assoofs_get_inode(struct super_block *sb, int ino)
{
    struct inode *inode;

    // 1. Obtener la informacion persistente del inodo ino
    struct assoofs_inode_info *inode_info;
    inode_info = assoofs_get_inode_info(sb, ino);

    // 2.Inicializar el inodo
    inode = new_inode(sb);

    // Asignar valores
    inode->i_ino = ino;               // numero de inodo
    inode->i_sb = sb;                 // puntero al superbloque
    inode->i_op = &assoofs_inode_ops; // direccion de una variable de tipo struct inode_operations previamente
                                      // declarada

    // Comprobar si es un directorio o un fichero
    if (S_ISDIR(inode_info->mode))
        inode->i_fop = &assoofs_dir_operations;
    else if (S_ISREG(inode_info->mode))
        inode->i_fop = &assoofs_file_operations;
    else
        printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");

    // Fecha del sistema a los campos
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode); // fechas.

    inode->i_private = inode_info;

    return inode;
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{

    // 1. Acceder al bloque de disco con el contenido del directorio apuntado por parent_inode
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    printk(KERN_INFO "Lookup request\n");
    printk(KERN_INFO "Lookup in: ino = %llu, b=%llu\n", parent_info->inode_no, parent_info->data_block_number);

    bh = sb_bread(sb, parent_info->data_block_number);

    // 2. Recorrer el contenido del directorio buscando la entrada cuyo nombre se corresponda con el que buscamos.
    // Si se localiza la entrada entonces debe creare el inodo correponsidente
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < parent_info->dir_children_count; i++)
    {
        if (!strcmp(record->filename, child_dentry->d_name.name))
        {
            struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Funcion auxiliar que obtine la informaci ́on de
                                                                           // un inodo a partir de su n ́umero de inodo.
            printk(KERN_INFO "Have file: %s, ino=%llu\n", record->filename, record->inode_no);
            inode_init_owner(sb->s_user_ns, inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
            d_add(child_dentry, inode);
            return NULL;
        }
        record++;
    }

    // 3.Devolver siempre NULL
    return NULL;
}

/*
 *   Permite actualizar la informacion persistente del superbloque cuando hay un cambio en memoria
 */

void assoofs_save_sb_info(struct super_block *vsb)
{

    // Leer de disco la información persistente del superbloque con sb bread y sobreescribir el campo
    // b_data con la información en memoria

    struct buffer_head *bh;
    struct assoofs_super_block_info *sb = vsb->s_fs_info; // Información persistente del superbloque en memoria

    printk(KERN_INFO "assoofs_save_sb_info request\n");

    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria

    // Para que el cambio pase a disco, marcar el buffer como sucio y sincronizar

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}

/*
 *   Permite obtener un blque libre
 */

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block)
{

    // Obtenemos la informacion persistente del superbloque
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;

    printk(KERN_INFO "assoofs_sb_get_a_freeblock request\n");

    // Recorremos el mapa de bits en busca de uno libre (bit=1)
    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
        if (assoofs_sb->free_blocks & (1 << i))
            break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posición
                   // del primer bloque libre
    *block = i;    // Escribimos el valor de i en la dirección de memoria indicada como segundo argumento en la función

    // Actuaziar el valor de free_block y guardar los cambios en el superbloque
    assoofs_sb->free_blocks &= ~(1 << i);
    assoofs_save_sb_info(sb);

    // Comprobar si es el valor de i
    printk(KERN_INFO "Freeblock --> %d\n", i);

    return 0;
}

/*
 *   Guardar en disco la informacion persistente del nuevo inodo
 */

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode)
{

    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;

    // Obtener el contador de inodos del superbloque
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;

    printk(KERN_INFO "assoofs_add_inode_info request\n");

    // leer de disco el bloque que contiene el almacen de inodos
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    // Obtener un nuevo puntero al final del almacen y escribir un nuevo valor al final
    inode_info = (struct assoofs_inode_info *)bh->b_data;
    inode_info += assoofs_sb->inodes_count;
    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));
    // Marcar el bloque como sucio y sincronizar
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    // Actualizar el contador de inodos de la informacion persistente del superbloque y guardar los cambios
    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);
}

/*
 *   Permitira obtener un puntero a la informacion persistente de un inodo concreto
 */

struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
    uint64_t count = 0;

    printk(KERN_INFO "assoofs_inode_info request\n");

    while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count)
    {
        count++;
        start++;
    }
    if (start->inode_no == search->inode_no)
    {
        // printk(KERN_INFO "i-nodo found. no-> &lld\n", start->inode_no);
        return start;
    }
    else
    {
        return NULL;
    }
}

/*
 *   Permite actualizar en disco la informacion persistente de un inodo
 */

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
    struct assoofs_inode_info *inode_pos;
    struct buffer_head *bh;

    printk(KERN_INFO "assoofs_save_inode_info request\n");

    // Obtener de disco el bloque que contiene el alamcen de inodos
    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    // Buscar los datos de inode_info en el almacen de inodos
    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);

    // Actualizar el inodo, marcar como sucio y sincronizar
    memcpy(inode_pos, inode_info, sizeof(*inode_pos));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    // 0 todo va bien
    return 0;
}

static int assoofs_create(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    // 1. Crear el nuevo i-nodo
    struct super_block *sb;
    struct buffer_head *bh;
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    uint64_t count;

    printk(KERN_INFO "New file request\n");

    // El nuevo inodo se asigna a traves de la iformacion persistente del superbloque
    sb = dir->i_sb;                                                           // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el número de inodos de la
                                                                              // información persistente del superbloque
    inode = new_inode(sb);
    inode->i_sb = sb;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_op = &assoofs_inode_ops;
    inode->i_ino = count + 1; // Asigno número al nuevo inodo a partir de count
    // BORRAR---------------------------------------------------
    // Para ver cuanto vale count y saber si tngo que restarle los dos inodos que ya estan en memoria
    printk(KERN_INFO "COUNT--------------------- %lld\n", count);

    // Comprobar que el valor de count no excede el numero maximo de de objetos soportados por assofs
    if (count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED - 2)
    {
        printk(KERN_INFO "Exceded max number of files\n");
        return -1;
    }

    // Guardar en el campo i_private la informacion persistente del i-nodo creando una nueva estructura
    // de assoofs_inode_info
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    inode->i_fop = &assoofs_file_operations; // Para indicar que las operaciones son sobre ficheros

    // Asignar propietario y permisos, guardar el nuevo inodo en el arbol de direcciones
    // Tuve que anniadir sb->s_user_ns por la signatura del metodo, en los apuntes no estaba
    inode_init_owner(sb->s_user_ns, inode, dir, mode);
    d_add(dentry, inode);

    // Hay que asignarle un bloque al nuevo inodo, por lo que habrá que consultar el mapa de bits del superbloque.
    // para ello funcion auxiliar (se utilizara mucho) assoofs_sb_get_a_freeblock a su vez, tendrá que actualizar
    // la información persistente del superbloque, en concreto el valor del campo free blocks. Esta operación
    // tambien se repite en más lugares por lo que se recomienda definir una función auxiliar: assoofs save sb info

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);

    // Guardar la informacion persistente del nuevo inodo en disco

    assoofs_add_inode_info(sb, inode_info);

    // Modificar el contenido del directorio padre, añadiendo una nueva entrada para el nuevo archivo

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    // Actualizar la información persistente del inodo padre indicando que ahora tiene un archivo más. Se recomienda definir
    // una función auxiliar para esta operación: assoofs save inode info. Para actualizar la información persistente de un
    // inodo es necesario recorrer el almacen y localizar dicho inodo, para ello se recomienda definir otra función auxiliar:
    // assoofs_search_inode_info.

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    // 0 todo ha ido bien
    return 0;
}

static int assoofs_mkdir(struct user_namespace *mnt_userns, struct inode *dir, struct dentry *dentry, umode_t mode)
{
    // 1. Crear el nuevo i-nodo
    struct super_block *sb;
    struct buffer_head *bh;
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    uint64_t count;

    printk(KERN_INFO "New directory request\n");

    // El nuevo inodo se asigna a traves de la iformacion persistente del superbloque
    sb = dir->i_sb;                                                           // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el número de inodos de la
                                                                              // información persistente del superbloque
    inode = new_inode(sb);
    inode->i_sb = sb;
    inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
    inode->i_op = &assoofs_inode_ops;
    inode->i_ino = count + 1; // Asigno número al nuevo inodo a partir de count
    // BORRAR---------------------------------------------------
    // Para ver cuanto vale count y saber si tngo que restarle los dos inodos que ya estan en memoria
    printk(KERN_INFO "COUNT--------------------- %lld\n", count);

    // Comprobar que el valor de count no excede el numero maximo de de objetos soportados por assofs
    if (count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED)
    {
        printk(KERN_INFO "Exceded max number of files\n");
        return -1;
    }

    // Guardar en el campo i_private la informacion persistente del i-nodo creando una nueva estructura
    // de assoofs_inode_info
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = S_IFDIR | mode; // El segundo mode me llega como argumento

    inode_info->dir_children_count = 0;
    inode->i_private = inode_info;
    inode->i_fop = &assoofs_dir_operations; // Para indicar que las operaciones son sobre directorios

    // Asignar propietario y permisos, guardar el nuevo inodo en el arbol de direcciones
    // Tuve que anniadir sb->s_user_ns por la signatura del metodo, en los apuntes no estaba
    inode_init_owner(sb->s_user_ns, inode, dir, inode_info->mode);
    d_add(dentry, inode);

    // Hay que asignarle un bloque al nuevo inodo, por lo que habrá que consultar el mapa de bits del superbloque.
    // para ello funcion auxiliar (se utilizara mucho) assoofs_sb_get_a_freeblock a su vez, tendrá que actualizar
    // la información persistente del superbloque, en concreto el valor del campo free blocks. Esta operación
    // tambien se repite en más lugares por lo que se recomienda definir una función auxiliar: assoofs save sb info

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);

    // Guardar la informacion persistente del nuevo inodo en disco

    assoofs_add_inode_info(sb, inode_info);

    // Modificar el contenido del directorio padre, añadiendo una nueva entrada para el nuevo archivo

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    // Actualizar la información persistente del inodo padre indicando que ahora tiene un archivo más. Se recomienda definir
    // una función auxiliar para esta operación: assoofs save inode info. Para actualizar la información persistente de un
    // inodo es necesario recorrer el almacen y localizar dicho inodo, para ello se recomienda definir otra función auxiliar:
    // assoofs_search_inode_info.

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    // 0 todo ha ido bien
    return 0;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent)
{

    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb;
    struct inode *root_inode;

    printk(KERN_INFO "assoofs_fill_super request\n");

    // DECLARAR SIEMPRE VARIABLES DE KERNEL AL INICIO DEL METODO PARA EVITAR WARNING (QUITAN 1 PUNTO)

    // 2.3.4 del guion de practicas

    // 1.- Leer la información persistente del superbloque del dispositivo de bloques

    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; // Casteo a la estructura
    brelse(bh);                                                 // Liberar memoria asignada

    // 2.- Comprobar los parámetros del superbloque
    if (assoofs_sb->magic != ASSOOFS_MAGIC)
    {
        printk("The magic number is wrong:%lld\n", assoofs_sb->magic);
        return -1;
    }

    if (assoofs_sb->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE)
    {
        printk("The block size is wrong:%lld\n", assoofs_sb->block_size);
        return -1;
    }

    printk("The magic number is :%lld, and the block size:%lld\n", assoofs_sb->magic, assoofs_sb->block_size);

    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    sb->s_magic = ASSOOFS_MAGIC;
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = &assoofs_sops;
    sb->s_fs_info = assoofs_sb;

    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)
    // Declaracion del inodo raiz
    root_inode = new_inode(sb);
    inode_init_owner(sb->s_user_ns, root_inode, NULL, S_IFDIR);

    // Asignacion de informacion al inodo
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;                                           // número de inodo
    root_inode->i_sb = sb;                                                                      // puntero al superbloque
    root_inode->i_op = &assoofs_inode_ops;                                                      // dirección de una variable de tipo struct inode_operations previamente
                                                                                                // declarada
    root_inode->i_fop = &assoofs_dir_operations;                                                // dirección de una variable de tipo struct file_operations previamente
                                                                                                // declarada. En la práctica tenemos 2: assoofs_dir_operations y assoofs_file_operations. La primera la
                                                                                                // utilizaremos cuando creemos inodos para directorios (como el directorio raı́z) y la segunda cuando creemos
                                                                                                // inodos para ficheros.
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = current_time(root_inode); // fechas.
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);           // Informacion persistente del inodo

    // Introducir el nuevo inodo en el arbol de inodos
    // Fijar inodo raiz en el superbloque, solo se realiza una vez

    sb->s_root = d_make_root(root_inode);

    // Devuelve 0 si todo va bien
    return 0;
}

/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{

    // 2.3.3 del guion de practicas

    struct dentry *ret;
    printk(KERN_INFO "assoofs_mount request\n");
    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de ret. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...

    if (IS_ERR(ret))
    {
        printk(KERN_ERR "Could not mount device with assoofs\n");
    }
    else
    {
        printk(KERN_INFO "Device mounted with assoofs\n");
    }

    return ret;
}

/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner = THIS_MODULE,
    .name = "assoofs",            // Nombre del modulo. Tiene que ser correcto sino no monta
    .mount = assoofs_mount,       // Funcion que debe llamar cuando se haga el montaje
    .kill_sb = kill_litter_super, // Al desmontar llama a la funcion de kill_litter_super (es funcion de la libreria)
};

/**
 * Registro de assoofs en el kernel y mensaje
 */
static int __init assoofs_init(void)
{

    ////2.3.2 del guion de practicas

    int ret;
    printk(KERN_INFO "assoofs_init request\n");
    ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    // Returns 0 on success, or a negative errno code on an error.

    if (ret != 0)
    {
        printk(KERN_INFO "assoofs has not been registered\n");
        return ret;
    }

    printk(KERN_INFO "Sucessfully registered assoofs");
    return ret;
}

/**
 * Eliminar informacion de assoofs del kernel y mensaje
 */
static void __exit assoofs_exit(void)
{

    ////2.3.2 del guion de practicas

    int ret;
    printk(KERN_INFO "assoofs_exit request\n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    // Returns 0 on success, or a negative errno code on an error.
    if (ret != 0)
    {
        printk(KERN_INFO "assoofs has not been unregistered\n");
    }
    else
    {
        printk(KERN_INFO "Sucessfully unregistered assoofs");
    }

    printk(KERN_INFO "Adios----------------------------------\n");
}

module_init(assoofs_init);
module_exit(assoofs_exit);
