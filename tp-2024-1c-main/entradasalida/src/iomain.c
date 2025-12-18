#include "io.h"

t_log* logger = NULL;
t_config* io_config_setup = NULL;
t_config* path_config = NULL;
int32_t err, memconn = 0, kerconn = 0, work_time = 0;
char *io_type_name = NULL, *io_name = NULL, *base_path_dialfs = NULL, *bitarray_addr = NULL;

t_dictionary* setup_io_dict, *operations_dict;

t_bitarray* block_state = NULL;
uint32_t block_size = 0, block_count = 0, compact_retard = 0, byte_length = 0;

FILE* bloques = NULL, *bitmap = NULL;

void* FULL = ((void*)1);

char* bloques_path = NULL, *bitmap_path = NULL;
char* key_first_block_config = "FIRST_BLOCK";
char* key_size_config = "SIZE";
void end_io(int a){
	liberar_conexion(memconn);
	liberar_conexion(kerconn);
	if(io_config_setup != NULL) config_destroy(io_config_setup);
	if(path_config != NULL) config_destroy(path_config);
	if(logger != NULL) log_destroy(logger);

	if(setup_io_dict != NULL) dictionary_destroy(setup_io_dict);
	if(operations_dict != NULL) dictionary_destroy(operations_dict);

	if(bitmap_path != NULL) bitmap = fopen(bitmap_path, "rb+");
	if(bitmap != NULL){ 
		fseek(bitmap, 0, SEEK_SET);
		fwrite(bitarray_addr, byte_length, 1, bitmap);
		fclose(bitmap);
	}

	if(block_state != NULL) bitarray_destroy(block_state);
	if(bitarray_addr != NULL) free(bitarray_addr);

	if(bloques_path != NULL) free(bloques_path); 
	if(bitmap_path != NULL) free(bitmap_path);

	abort();
}


void* sleep_io(t_pcb* pcb){
	log_info(logger, "PID: %d - Operacion: sleep", pcb->pid);
	int32_t work_units = strtol(list_get(pcb->instr->operands, 1), NULL, 10);
	usleep(work_units*work_time*1000);
	return ((void*)1);
}

void generic_setup(void){
	work_time = config_get_int_value(io_config_setup, "WORK_TIME");
	dictionary_put(operations_dict, "IO_GEN_SLEEP", &sleep_io);
}
void dump_buffer(void) {
    int ch;    
    while ( (ch = fgetc(stdin)) != EOF && ch != '\n');
}
void* stdin_io(t_pcb* pcb){
	log_info(logger, "PID: %d - Operacion: stdin", pcb->pid);
	
	uint32_t nchars = strtol(list_get(pcb->instr->operands, 2), NULL, 10);
	
	char* readv = calloc(100, 1);
	if(readv == NULL)  end_io(0);
	
	log_info(logger, "Ingresar texto");
	if(fgets(readv, nchars+1, stdin) == NULL) end_io(0);
	dump_buffer();

	for(int i=3; i<nchars+3; i++){
		uint32_t phdir = strtol(list_get(pcb->instr->operands, i), NULL, 10);
		if(send_write_byte_to_mem(memconn, readv+i-3, phdir, pcb->pid) == -1){
			end_io(0);
		}
	}
	free(readv);
	return ((void*)1);
}

void stdin_setup(void){
	dictionary_put(operations_dict, "IO_STDIN_READ", &stdin_io);
}

void* stdout_io(t_pcb* pcb){
	log_info(logger, "PID: %d - Operacion: stdout", pcb->pid);
	
	uint32_t nchars = strtol(list_get(pcb->instr->operands, 2), NULL, 10);
	char* readv = malloc(nchars+1);
	readv[nchars] = '\0'; 

	for(int i=3; i<nchars+3; i++){
		uint32_t phdir = strtol(list_get(pcb->instr->operands, i), NULL, 10);
		if(send2_uint32t(memconn, phdir, pcb->pid, READ_BYTE_MEM) == -1){
			printf("error al mandar direccion fisicaa leer"); return NULL;
		}
		recv(memconn, readv+i-3, 1, MSG_WAITALL);
	}
	log_info(logger, "El string leido es %s", readv);

	free(readv);
	return ((void*)1);

}
void stdout_setup(void){
	dictionary_put(operations_dict, "IO_STDOUT_WRITE", &stdout_io);
}
void update_bitmap(void){
	bitmap = fopen(bitmap_path, "rb+");
	if(bitmap != NULL){ 
		fseek(bitmap, 0, SEEK_SET);
		fwrite(bitarray_addr, byte_length, 1, bitmap);
		fclose(bitmap);
	}
}
uint32_t get_file_size_block(uint32_t file_size_byte){
	return ( (file_size_byte == 0) ? (1) : (ceil((double)file_size_byte/(double)block_size)) );
}
void* fs_create(t_pcb* pcb){
	
	usleep(work_time*1000);
	int32_t free_block = -1;
	for(int i=0; i < block_count; i++){
		if(bitarray_test_bit(block_state, i) == false){
			free_block = i;
			bitarray_set_bit(block_state, i);
			break;
		}
	}
	if(free_block == -1) return NULL;
	
	char* file_name = list_get(pcb->instr->operands, 1);
	log_info(logger, "PID: %d - Crear archivo: %s", pcb->pid, file_name);

	char* path = string_concat(base_path_dialfs, file_name);
	
	FILE* file = fopen(path, "w");
	if(file == NULL) return NULL;

	fprintf(file, "%s=%d \n", key_first_block_config,free_block);
	fprintf(file, "%s=0 \n", key_size_config);

	if(fclose(file) != 0) end_io(0);
	free(path);
	update_bitmap();
	return ((void*)1);
}
void* fs_delete(t_pcb* pcb){
	usleep(work_time*1000);
	char* file_name = list_get(pcb->instr->operands, 1);
	log_info(logger, "PID: %d - Eliminar archivo: %s", pcb->pid, file_name);

	char* path = string_concat(base_path_dialfs, file_name);

	t_config* config_file = config_create(path);
	if(config_file == NULL) end_io(0);

	uint32_t first_block = config_get_int_value(config_file, key_first_block_config);
	uint32_t size_byte = config_get_int_value(config_file, key_size_config);
	uint32_t size_block = get_file_size_block(size_byte);

	bitarray_clean_bit(block_state, first_block);
	for(int i = first_block; i<first_block+size_block; i++){
		bitarray_clean_bit(block_state, i);
	}

	remove(path);
	free(path);
	config_destroy(config_file);
	update_bitmap();
	return ((void*)1);
}
void* fs_write(t_pcb* pcb){
	usleep(work_time*1000);
	char* file_name = list_get(pcb->instr->operands, 1);

	char* path = string_concat(base_path_dialfs, file_name);
	t_config* config_file = config_create(path);
	uint32_t first_block = config_get_int_value(config_file, key_first_block_config);
	
	uint32_t nbytes = strtol(list_get(pcb->instr->operands, 3), NULL, 10);
	uint32_t file_ptr = strtol(list_get(pcb->instr->operands, 4), NULL, 10);
	
	char* readv = calloc(1, nbytes+1);
	readv[nbytes] = '\0';

	for(int i=5; i<nbytes+5; i++){
		uint32_t phdir = strtol(list_get(pcb->instr->operands, i), NULL, 10);
		if(send2_uint32t(memconn, phdir, pcb->pid, READ_BYTE_MEM) == -1){
			printf("error al mandar direccion fisicaa leer"); return NULL;
		}

		if( recv(memconn, readv+i-5, 1, MSG_WAITALL) == -1)
			end_io(0);
	
		}
	
	log_info(logger, "PID: %d - Escibir Archivo: %s - Tamaño a Leer: %d - Puntero Archivo: %d", pcb->pid, file_name, nbytes, file_ptr);

	bloques = fopen(bloques_path, "rb+");
	if(bloques == NULL) end_io(0);
	fseek(bloques, 0, SEEK_SET);

	if(fseek(bloques, file_ptr+first_block*block_size, SEEK_SET) != 0) end_io(0);
	
	if(nbytes != 0)
		fwrite(readv, nbytes, 1, bloques);

	if(fclose(bloques) != 0) end_io(0);
	free(readv);
	free(path);
	config_destroy(config_file);
	update_bitmap();
	return ((void*)1);
}

void* fs_read(t_pcb* pcb){
	usleep(work_time*1000);
	char* file_name = list_get(pcb->instr->operands, 1);
	char* path = string_concat(base_path_dialfs, file_name);
	t_config* config_file = config_create(path);
	uint32_t first_block = config_get_int_value(config_file, key_first_block_config);

	
	uint32_t nbytes = strtol(list_get(pcb->instr->operands, 3), NULL, 10);
	uint32_t file_ptr = strtol(list_get(pcb->instr->operands, 4), NULL, 10);

	char* read_data = calloc(1, nbytes);

	bloques = fopen(bloques_path, "rb+");
	if(bloques == NULL) end_io(0);
	fseek(bloques, 0, SEEK_SET);

	if(fseek(bloques, file_ptr+first_block*block_size, SEEK_SET)!= 0) end_io(0);

	if(nbytes != 0 && fread(read_data, nbytes, 1, bloques) == 0) end_io(0);
			
	for(int i=5; i<nbytes+5; i++){
		uint32_t phdir = strtol(list_get(pcb->instr->operands, i), NULL, 10);
		if(send_write_byte_to_mem(memconn, read_data+i-5, phdir, pcb->pid) == -1){
			end_io(0);
		}
	}
	
	if(fclose(bloques) != 0) end_io(0);
	free(read_data);
	free(path);
	config_destroy(config_file);

	log_info(logger, "PID: %d - Leer Archivo: %s - Tamaño a Leer: %d - Puntero Archivo: %d", pcb->pid, file_name, nbytes, file_ptr);
	update_bitmap();
	return ((void*)1);
}



char* get_file_name_with_first_block(uint32_t first_block_of_file){
	DIR *dr;
	struct dirent *en;
	dr = opendir(base_path_dialfs); //open all directory
	if(dr == NULL) end_io(0);
	
	char* path = NULL;
	
	while ((en = readdir(dr)) != NULL) {
		char** substrings = string_split(en->d_name, ".");
		char* extension = substrings[1];
		if(strcmp("txt", extension) != 0){
			string_array_destroy(substrings);
			continue;
		}
		path = string_concat(base_path_dialfs, en->d_name);
		string_array_destroy(substrings);

		t_config* config_file = config_create(path);
		if(config_file == NULL) end_io(0);
		
		uint32_t first_block_config = config_get_int_value(config_file, key_first_block_config);
		config_destroy(config_file);
		
		if(first_block_config == first_block_of_file)
			break;
		
		free(path);	
		path = NULL;
    }
    closedir(dr);
	return ( (path == NULL) ? NULL : path);
}
void* remove_file(uint32_t first_block, uint32_t block_size_file){
	bloques = fopen(bloques_path, "rb+");
	if(bloques == NULL) end_io(0);
	fseek(bloques, 0, SEEK_SET);

	bitarray_clean_bit(block_state, first_block);
	for(int i=first_block; i<first_block+block_size_file; i++){
		bitarray_clean_bit(block_state, i);
	}
	//voy a buscar la informacion y la guardo antes de compactar
	if(fseek(bloques, first_block*block_size, SEEK_SET)!= 0) end_io(0);

	uint32_t byte_size_file = block_size_file*block_size;
	void* file_data = calloc(1, byte_size_file);
			
	if(byte_size_file != 0 && fread(file_data, byte_size_file, 1, bloques) == 0)
		end_io(0);
	
	if(fclose(bloques) != 0) end_io(0);
	return file_data;
}

uint32_t move_block(uint32_t file_new_first_block, uint32_t file_old_first_block){
	char* file_name = get_file_name_with_first_block(file_old_first_block);
	if(file_name == NULL){
		printf("N0 existe archivo que empieze en ese bloque");
		end_io(0);
	}
	
	t_config* config_file = config_create(file_name);
	if(config_file == NULL) end_io(0);
	
	uint32_t file_size_byte = config_get_int_value(config_file, key_size_config);
	uint32_t file_size_block = get_file_size_block(file_size_byte); 
	

	void* file_data = remove_file(file_old_first_block, file_size_block);

	bloques = fopen(bloques_path, "rb+");
	if(bloques == NULL) end_io(0);
	fseek(bloques, 0, SEEK_SET);

	if(fseek(bloques, file_new_first_block*block_size, SEEK_SET)!= 0) end_io(0);
	
	if(file_size_byte != 0)
		fwrite(file_data, file_size_byte, 1, bloques);

	bitarray_set_bit(block_state, file_new_first_block);
	for(int i=file_new_first_block; i<file_new_first_block+file_size_block; i++){
		bitarray_set_bit(block_state, i);
	}
	
	FILE* file_metadata = fopen(file_name, "w");
	if(file_metadata == NULL) end_io(0);

	char str_first_block[30];
	sprintf(str_first_block, "%u", file_new_first_block);
	fprintf(file_metadata, "%s=%s\n", key_first_block_config, str_first_block);
	
	char* file_size_byte_str = config_get_string_value(config_file, key_size_config);
	fprintf(file_metadata, "%s=%s\n", key_size_config, file_size_byte_str);

	if(fclose(file_metadata) != 0) end_io(0);


	free(file_name);
	free(file_data);
	config_destroy(config_file);
	if(fclose(bloques) != 0) end_io(0);

	return file_new_first_block+file_size_block;
}

void fs_compact(void){
	usleep(1000*compact_retard);
	
	bool found_free = false;
	uint32_t start_of_free_blocks = 0;
	for(int i=0; i<block_count; i++){
		if(bitarray_test_bit(block_state, i) == false && found_free == false){
			found_free = true;
			start_of_free_blocks = i;
		}
		else if(bitarray_test_bit(block_state, i) == true && found_free == true){
			start_of_free_blocks = move_block(start_of_free_blocks, i);
			i = start_of_free_blocks-1;
			found_free = false;
		}
		
	}
	
} 

uint32_t free_blocks_get(void){
	uint32_t free_blocks = 0;
	for(int i=0; i<block_count; i++){
		if(bitarray_test_bit(block_state, i) == false){
			free_blocks++;
		}
	}
	return free_blocks;
}


void* fs_truncate(t_pcb* pcb){
	
	usleep(work_time*1000);
	char* new_size_byte_str = list_get(pcb->instr->operands, 2);
	uint32_t new_byte_size = strtol(new_size_byte_str, NULL, 10);
	uint32_t new_block_size = get_file_size_block(new_byte_size); 
	
	char* file_name = list_get(pcb->instr->operands, 1);
	log_info(logger, "PID: %d - Truncar archivo: %s - - Tamaño: %s", pcb->pid, file_name, new_size_byte_str);
	char* path = string_concat(base_path_dialfs, file_name);

	t_config* config_file = config_create(path);
	if(config_file == NULL) end_io(0);

	uint32_t old_first_block = config_get_int_value(config_file, key_first_block_config);
	uint32_t old_byte_size_file = config_get_int_value(config_file, key_size_config);
	uint32_t old_block_size = get_file_size_block(old_byte_size_file);

	uint32_t new_first_block = old_first_block;

	uint32_t free_blocks = free_blocks_get();
	int32_t amount_of_blocks_to_add = new_block_size-old_block_size;
	if(amount_of_blocks_to_add > free_blocks) return NULL; //no erspaci

	if(amount_of_blocks_to_add != 0){
		if(new_block_size>old_block_size){
			for(int i=old_first_block+old_block_size; i< old_first_block+new_block_size; i++){
				if(bitarray_test_bit(block_state, i) == false) 
					amount_of_blocks_to_add--;
			}
			if(amount_of_blocks_to_add == 0){
				for(int i=old_first_block+old_block_size; i< old_first_block+new_block_size; i++){
					bitarray_set_bit(block_state, i);
				}
			}
			else{
				void* file_data = remove_file(old_first_block, old_block_size);
				
				log_info(logger, "PID: %d - Inicio Compactación", pcb->pid);
				fs_compact();
				log_info(logger, "PID: %d - FIn Compactación", pcb->pid);

				int i =0;
				for(; i<block_count; i++){
					if(bitarray_test_bit(block_state, i) == false) break;
				}
				new_first_block = i;

				bitarray_set_bit(block_state, new_first_block);
				for(; i<new_first_block+new_block_size; i++){
					bitarray_set_bit(block_state, i);
				}
				bloques = fopen(bloques_path, "rb+");
				if(bloques == NULL) end_io(0);
				fseek(bloques, 0, SEEK_SET);

				if(fseek(bloques, new_first_block*block_size, SEEK_SET)!= 0) end_io(0);
			
				if(old_byte_size_file != 0)
					fwrite(file_data, old_byte_size_file, 1, bloques);
				
				free(file_data);
				if(fclose(bloques) != 0) end_io(0);
			}

		}
		else if (new_block_size<old_block_size){
			for(int i=old_block_size+old_first_block-1; i>=old_first_block+new_block_size ; i--){
				bitarray_clean_bit(block_state, i);
			}
		}
	}

	char str_first_block[30];
	sprintf(str_first_block, "%u", new_first_block);

	FILE* file_metadata = fopen(path, "w");
	if(file_metadata == NULL) return NULL;

	fprintf(file_metadata, "%s=%s\n", key_first_block_config, str_first_block);
	fprintf(file_metadata, "%s=%s\n", key_size_config, new_size_byte_str);
	if(fclose(file_metadata) != 0) end_io(0);

	config_destroy(config_file);
	free(path);
	update_bitmap();

	return ((void*)1);
}

void dialfs_setup(void){
	dictionary_put(operations_dict, "IO_FS_CREATE", &fs_create);
	dictionary_put(operations_dict, "IO_FS_DELETE", &fs_delete);
	dictionary_put(operations_dict, "IO_FS_TRUNCATE", &fs_truncate);
	dictionary_put(operations_dict, "IO_FS_WRITE", &fs_write);
	dictionary_put(operations_dict, "IO_FS_READ", &fs_read);
	work_time = config_get_int_value(io_config_setup, "WORK_TIME");
	block_count = config_get_int_value(io_config_setup, "BLOCK_COUNT");
	block_size = config_get_int_value(io_config_setup, "BLOCK_SIZE");
	compact_retard = config_get_int_value(io_config_setup, "COMPACT_RETARD");
	base_path_dialfs = config_get_string_value(io_config_setup, "BASE_PATH_DIALFS");

	bloques_path = string_concat(base_path_dialfs, "bloques.dat");

	bloques = fopen(bloques_path, "a+");
	if(bloques == NULL) end_io(0);
	if(ftruncate(fileno(bloques), block_size*block_count)!= 0) end_io(0);
	fseek(bloques, 0, SEEK_SET);
	if(fclose(bloques) != 0) end_io(0);

	byte_length = ceil((double)block_count/(double)8);
	
	bitmap_path = string_concat(base_path_dialfs, "bitmap.dat"); ;
	bitmap = fopen(bitmap_path, "a+");
	if(bitmap == NULL) end_io(0);

	if(ftruncate(fileno(bitmap), byte_length) != 0) end_io(0);
	
	fseek(bitmap, 0, SEEK_SET);
	if(fclose(bitmap) != 0) end_io(0);

	bitarray_addr = calloc(1, byte_length);
	if(bitarray_addr == NULL) end_io(0);

	bitmap = fopen(bitmap_path, "rb+");
	if(bitmap == NULL) end_io(0);
	fseek(bitmap, 0, SEEK_SET);

	if(fread(bitarray_addr, byte_length, 1, bitmap) == 0) end_io(0);

	if(fclose(bitmap) != 0) end_io(0);

	block_state = bitarray_create_with_mode(bitarray_addr, byte_length, MSB_FIRST);
	

	
	
}

void setup__setup_io_dictionary(void){
	setup_io_dict = dictionary_create();
	operations_dict = dictionary_create();
	dictionary_put(setup_io_dict, "GENERIC", &generic_setup);
	dictionary_put(setup_io_dict, "STDIN", &stdin_setup);
	dictionary_put(setup_io_dict, "STDOUT", &stdout_setup);
	dictionary_put(setup_io_dict, "DIALFS", &dialfs_setup);
}



int32_t send_io_info(void){
	uint32_t lenght_name = strlen(io_name)+1;
	uint32_t lenght_type = strlen(io_type_name)+1;
	
	t_buff* buff = buff_create(sizeof(uint32_t)*2+ lenght_name + lenght_type );
	buff_add_string(buff, lenght_name, io_name);
	buff_add_string(buff, lenght_type, io_type_name);

	t_list* keys_list = dictionary_keys(operations_dict);
	t_list_iterator* iterator = list_iterator_create(keys_list);

	while(list_iterator_has_next(iterator)){
		buff_add_string_dynamic(buff, list_iterator_next(iterator));
	}
	
	list_iterator_destroy(iterator); list_destroy(keys_list);
	
	t_pkg* pkg = pkg_create(NEWIO, buff);
	int32_t err = send_pkg(pkg, kerconn);
	pkg_destroy(pkg);
	if(err == -1) printf("Error al enviar io info");
	return err;
}

void manage_io_operations(void){
	while(1){
		if(recv_operacion(kerconn) != PCB){
			return;
		}
		t_pcb* pcb = pcb_recv(kerconn);

		void* (*operation)(t_pcb*) = dictionary_get(operations_dict, pcb->instr->code);
		if(operation == NULL){
			printf("Esa operacion no existe para esta interfaz");
			if(send_operacion(kerconn, ERROR) == -1){
				printf("Send operacion error"); return;
			}
		}
		
		if( operation(pcb) == NULL){
			if(send_operacion(kerconn, ERROR) == -1){
				printf("Send operacion error, salio mal"); 
				end_io(0);
			}
		}
		pcb_destroy(pcb);

		if(send_operacion(kerconn, OK) == -1){
			printf("Send operacion ok, se mando mal");
			end_io(0);
		}

		
		//le MANDO CUALQUIER COSA solo par avisar que termine
	}
}

//primero paso el path y despues el nombre
int main(int argc, char** argv){
	signal(SIGINT, &end_io);
	
	if(argc < 2){
		printf("Falta pasar parametros de la interfaz");
		end_io(0);
	}
	/* ---------------- LOGGING ---------------- */
    t_log_level info = LOG_LEVEL_INFO;

	logger = log_create("io.log", "IO", true, info);
	if(logger == NULL){
		printf("Logger Error IO");
		end_io(0);
	}
	 /* ---------------- ARCHIVOS DE CONFIGURACION ---------------- */
	
	path_config = config_create("../general.config");
	if(path_config == NULL){
		printf("Config Error MEMORY");
		end_io(0);
	}
	
	char* ipmem = config_get_string_value(path_config, "IPMEM");
	char* memport = config_get_string_value(path_config, "MEMPORT");
	char* mempass= config_get_string_value(path_config, "MEMPASS");

	char* ipker = config_get_string_value(path_config, "IPKER");
	char* kerport = config_get_string_value(path_config, "KERPORT");
	char* kerpass = config_get_string_value(path_config, "KERPASS");

	

	//CREAR CONEXION DE CLIENTE DEL IO A MEM
	memconn = connect_and_handshake(ipmem, memport, mempass);
	if(memconn == 1){
		printf("Error al hacer conexion con memoria");
		end_io(0);
	}
	
	//CREAR CONEXION DE CLIENTE DEL IO A KERNEL
	kerconn = connect_and_handshake(ipker, kerport, kerpass);
	if(kerconn == 1){
		printf("Error al hacer conexion con el kernel");
		end_io(0);
	}

	//IO INTERFACE SETUP
		
	io_config_setup = config_create(argv[1]); // ./configs/stdin.config
	if(io_config_setup == NULL){
		printf("Config Error IO");
		end_io(0);
	}
	io_type_name = config_get_string_value(io_config_setup, "INTERFACE_TYPE");

	io_name = argv[2];
	setup__setup_io_dictionary();
	void (*setup)(void)  = dictionary_get(setup_io_dict, io_type_name); 
	setup();

	if(send_io_info() == -1){
		printf("Error enviando info necesaria para el funcionamiento de io");
		end_io(0);
	}

	manage_io_operations();
}
