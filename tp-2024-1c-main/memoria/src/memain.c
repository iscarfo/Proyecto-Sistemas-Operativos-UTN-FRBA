#include "mem.h"

t_log* logger=NULL;
t_config* config=NULL;
t_config* path_config = NULL;
int32_t err, response_delay=0;
t_queue_mutex *page_tables_queue=NULL, *programs=NULL; 
pthread_mutex_t mutex_log;
char *memport=NULL, *mempass=NULL, *instr_path=NULL, *test_path = NULL;;


void* user_mem = NULL;
uint32_t mem_size=0, page_size=0, num_of_pages=0, max_frame = 0;
t_bitarray* frame_state = NULL;

void end_mem(int a){
	if(config != NULL) config_destroy(config);
	if(logger != NULL) log_destroy(logger);
	if(path_config != NULL) config_destroy(path_config);

	destroy_queue_mutex(programs, &destroy_program);
	destroy_queue_mutex(page_tables_queue, &page_table_destroy);
	
	free(user_mem);
	bitarray_destroy(frame_state);

	pthread_mutex_destroy(&mutex_log);
	abort();
}
void show_string(void* string){
	if(printf_mutex(mutex_log, "\n %s \n", (char*) string) == -1) end_mem(0);
}
void* store_program(char* path, uint32_t pid){
	t_program* program = create_program(pid);
	if(path == NULL) return NULL;
	
	char* full_path = string_concat(test_path, path);

	FILE* fptr = fopen(full_path, "r");
	if (fptr == NULL){
		if(printf_mutex(mutex_log, "Error al abrir el archivo de script\n") == -1) end_mem(0);
		return NULL;
	}
	free(full_path);

	char line [256];
	while (fgets(line, sizeof(line), fptr) != NULL){
		char* instr = malloc(strlen(line)+1);
		if(instr == NULL){
			if(printf_mutex(mutex_log, "Fallo al crear programa") == -1) end_mem(0);
			destroy_program(program);
			return NULL;
		}
		memcpy(instr, line, strlen(line));
		
		if(instr[strlen(line)-1] != '\n'){
			instr[strlen(line)] = '\0';
		}
		else{
			instr[strlen(line)-1] = '\0';
		}
		
		add_queue_mutex(program->instrs_list, instr);
	}
	fclose(fptr);
	
	iterate_queue_mutex(program->instrs_list, show_string);
	add_queue_mutex(programs, program);

	return ((void*)1);
}
t_program* find_program_by_pid(uint32_t pid){

	bool is_the_one(void* program){	
		return  ((t_program*)program)->pid == pid;
	}
	
	return find_queue_mutex(programs, (void*) is_the_one);
}

char* search_instr(t_instr_searcher* instr_searcher){
	t_program* program = find_program_by_pid(instr_searcher->pid); 
	if(program == NULL){
		if(printf_mutex(mutex_log, "Program con ese pid no existe en memoria") == -1) end_mem(0);
		return NULL;
	}
	if( instr_searcher->pc >= size_queue_mutex(program->instrs_list) ){
		if(printf_mutex(mutex_log, "PC out of bounds of program") == -1) end_mem(0);
		return NULL;
	}
	char* instr = get_queue_index_mutex(program->instrs_list, instr_searcher->pc);
	if(instr == NULL) if(printf_mutex(mutex_log, "No se encontro instruccion en ese pc") == -1) end_mem(0);
	
	return instr;
}
t_page_table* page_table_find_by_pid(uint32_t pid){
	bool is_the_one(void* page_table){	
		return  ((t_page_table*)page_table)->pid == pid;
	}
	
	return find_queue_mutex(page_tables_queue, (void*) is_the_one);

}
void* frame_search(t_frame_searcher* frame_searcher){
	t_page_table* page_table = NULL; page_table = page_table_find_by_pid(frame_searcher->pid);


	return list_get(page_table->page_frame_entries, frame_searcher->page_num);
}

uint32_t resize_process(uint32_t bytes_requested, uint32_t pid){

	uint32_t new_num_pages = ceil( bytes_requested/(double)page_size);
	
	t_page_table* page_table = NULL; page_table = page_table_find_by_pid(pid);

	
	if(new_num_pages > page_table->max_num_of_pages){
		return 1;
	}
	if(new_num_pages == list_size(page_table->page_frame_entries)){
		return 0;
	}
	
	if(new_num_pages > list_size(page_table->page_frame_entries)){
		if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
		log_info(logger, " “PID: %d - Tamaño Actual: %d - Tamaño a Ampliar: %d", pid, list_size(page_table->page_frame_entries), new_num_pages);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);
		for(int i=0; new_num_pages!=0 && i <= max_frame; i++){
			if(bitarray_test_bit(frame_state, i) == false){
				uint32_t* val = malloc(sizeof(uint32_t));
				*val = i;
				list_add(page_table->page_frame_entries, val);
				new_num_pages--;
				bitarray_set_bit(frame_state, i);
			}

		}
		return new_num_pages;
	}
	else {
		uint32_t out = list_size(page_table->page_frame_entries) - new_num_pages;
		if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
		log_info(logger, "PID: %d - Tamaño Actual: %d - Tamaño a Reducir: %d", pid, list_size(page_table->page_frame_entries), new_num_pages);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);
		
		while(out){
			uint32_t* frame = list_remove(page_table->page_frame_entries, page_table->page_frame_entries->elements_count-1);
			bitarray_clean_bit(frame_state, *frame);
			free(frame);
			out--;
		}
		return 0;
	}

}
	
	




void* serve_client(void *clientptr){
	int32_t* client = clientptr;

	if(handshake_servidor(*client, mempass) == -1){
		if(printf_mutex(mutex_log, "Handshake salio mal") == -1) end_mem(0);
		return NULL;
	}
	int i = 1;
	op_code code;
	do{
		code = recv_operacion(*client);
		usleep(response_delay*1000);
		switch (code){
			
			case CLOSE:
				if(printf_mutex(mutex_log, "El cliente se desconecto") == -1) end_mem(0);
				--i;
			break;
			case INSTR_SEARCHER: {
				t_instr_searcher* instr_searcher = recv_instr_seacrher(*client);
				if(instr_searcher == NULL){
					if(printf_mutex(mutex_log, "Error al recibir el instr searcher del que habia que buscar instruccion") == -1) end_mem(0);
					break;
				}
				char* instr = search_instr(instr_searcher);
				if(instr == NULL){
					instr = "EOF";
				}
				if( send_string(*client, instr, INSTRUCTION) == -1){
					if(printf_mutex(mutex_log, "Error when sending instr to cpu") == -1) end_mem(0);
				}
				free(instr_searcher);
			break;
			}
			case PROGRAM:

				char* path = NULL; uint32_t pid;
				if(recv_string_uint32(*client, &path, &pid) == NULL){
					if(printf_mutex(mutex_log, "Error al recbir path y pid en mem") == -1) end_mem(0);
					end_mem(0);
				}

				void* result = store_program(path, pid);
				free(path);
				if(result == NULL){
					if(printf_mutex(mutex_log, "Error al recbir path y pid en mem") == -1) end_mem(0);
					end_mem(0);
				}
				t_page_table* page_table = page_table_create(num_of_pages, pid);
				if(page_table == NULL){
					end_mem(0);
				}
				if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
				log_info(logger,  "Creaciónde Tabla de Páginas: “PID: %d - Tamaño: %d", pid, num_of_pages);
				if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);

				add_queue_mutex(page_tables_queue, page_table);

			break;
			
			case DELETE_PROGRAM: {
				uint32_t pid = 0; 
				if(recv_uint32(*client, &pid) == NULL){
					if(printf_mutex(mutex_log, "Error al recibir el pcb que habia que borrar") == -1) end_mem(0);
					break;
				}
				bool is_the_one(void* vprogram){	
					t_program* program = vprogram;
					return  program->pid == pid;
				}
				remove_and_destroy_by_condition_queue_mutex(programs, is_the_one, destroy_program);
				
				bool is_th_one(void* page_table){
					return  ((t_page_table*)page_table)->pid == pid;
				}
				t_page_table* page_table = find_queue_mutex(page_tables_queue, is_th_one);
				if(page_table != NULL){
					if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
					log_info(logger, "Destrucción de Tabla de Páginas: “PID: %d - Tamaño: %d", pid, page_table->max_num_of_pages);
					if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);
				}
				remove_and_destroy_by_condition_queue_mutex(page_tables_queue, is_th_one, page_table_destroy);
				
			break;
			}

			case FRAME_SEARCHER: {
				t_frame_searcher* frame_searcher = t_frame_searcher_recv(*client);
				if(frame_searcher == NULL){
					if(printf_mutex(mutex_log, "Error al recibir el frame searcher") == -1) end_mem(0);
					break;
				}
				uint32_t* frame = frame_search(frame_searcher);

				if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
				log_info(logger, "Acceso a Tabla de Páginas: “PID: %d - Pagina: %d - Marco: %d", frame_searcher->pid, frame_searcher->page_num, *frame);
				if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);

				if( send_uint32t(*client, *frame, FRAME) == -1){
					if(printf_mutex(mutex_log, "Error al enviar el frame num") == -1) end_mem(0);
				}
				t_frame_searcher_destroy(frame_searcher);

			break;
			}

			case READ_BYTE_MEM: {
				uint32_t phdir = 0; uint32_t pid = 0; 
				if(recv2_uint32(*client, &phdir, &pid) == NULL){
					if(printf_mutex(mutex_log, "Error al recibir phdir que habia q leer") == -1) end_mem(0);
					break;
				}
				if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
				log_info(logger, "PID: %d- Accion:Leer- Direccion fisica: %d - Tamaño: 1byte", pid, phdir);
				if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);

				send(*client,  user_mem+phdir, 1, 0);
			break;
			}
			case WRITE_BYTE_MEM: {
				uint32_t phdir = 0; uint32_t pid = 0;
				t_buff* buff = recv_buff(*client);
				if(buff == NULL){
					if(printf_mutex(mutex_log, "Error al recibir el buff de instr searcher") == -1) end_mem(0);
					break;
				}
				phdir = buff_read_uint32(buff);
				pid = buff_read_uint32(buff);
				if(pthread_mutex_lock(&mutex_log) != 0) end_mem(0);
				log_info(logger, "PID: %d- Accion:ESCRIBIR- Direccion fisica: %d - Tamaño: 1byte", pid, phdir);
				if(pthread_mutex_unlock(&mutex_log) != 0) end_mem(0);

				char data; 
				buff_read(buff, &data, sizeof(char));

				memcpy(user_mem+phdir, &data, sizeof(char));
				buff_destroy(buff);
				break;
			}
			case RESIZE: {
				uint32_t bytes = 0;
				uint32_t pid = 0;
				if(recv2_uint32(*client, &bytes, &pid) == NULL)
					send(*client, &err, sizeof(uint32_t), 0);
				
				if(resize_process(bytes, pid) !=0){
					uint32_t err = 1;
					send(*client, &err, sizeof(uint32_t), 0);
				}
				else{
					uint32_t ok = 0;
					send(*client, &ok, sizeof(uint32_t), 0);
				}
			break;
			}

				
			default:
				log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
			
		} 
	} while(i);
	
	free(client);
	return NULL;
}


int main(int argc, char** argv){
	signal(SIGINT, &end_mem);
	if(argc < 1){
		if(printf_mutex(mutex_log, "Falta pasar parametros de la interfaz") == -1) end_mem(0);
		end_mem(0);
	}
	programs = create_queue_mutex();
	page_tables_queue = create_queue_mutex();

	if( pthread_mutex_init(&mutex_log, NULL) != 0) end_mem(0);
	
	/* ---------------- LOGGING ---------------- */
	t_log_level info = LOG_LEVEL_INFO;
	logger = log_create("mem.log", "MEMORY", true, info);
	if(!logger){
		if(printf_mutex(mutex_log, "Error logger MEM") == -1) end_mem(0);
		end_mem(0);
	}
  /* ---------------- ARCHIVOS DE CONFIGURACION ---------------- */
	
	path_config = config_create("../general.config");
	if(path_config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_mem(0);
		end_mem(0);

	}
	
	memport = config_get_string_value(path_config, "MEMPORT");
	mempass = config_get_string_value(path_config, "MEMPASS");
	test_path = config_get_string_value(path_config, "TEST_PATH");

	
	config = config_create(argv[1]); // ejemplo ../generalfs.config
	if(config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_mem(0);
		end_mem(0);

	}

	response_delay = config_get_int_value(config, "RESPONSE_DELAY");
	page_size = config_get_int_value(config, "PAGE_SIZE");
	mem_size = config_get_int_value(config, "MEM_SIZE");
	 
	if(mem_size%page_size != 0){
		if(printf_mutex(mutex_log, "el tamanio de mem no es multiplo del tamanio d epag") == -1) end_mem(0);
		end_mem(0);
	}
	//SETUP USER MEM
	num_of_pages = mem_size/page_size;

	float nbytes = num_of_pages/(float)8;
	uint32_t round_nbytes = ceil(nbytes);
	
	char* data = calloc(round_nbytes, 1);

	frame_state = bitarray_create_with_mode(data, round_nbytes, MSB_FIRST);
 
	max_frame = num_of_pages -1;

	user_mem = calloc(mem_size, 1);
	if(user_mem == NULL){
		end_mem(0);
	}

	//SOCKET SERVER
	int32_t server = start_servr(memport);
	if(server == -1){
		if(printf_mutex(mutex_log, "Error al crear servidor memoria") == -1) end_mem(0);
		end_mem(0);
	}
	while(1){
		int32_t client = accept(server, NULL, NULL);
		if(client == -1){ 
			if(printf_mutex(mutex_log, "Error al aceptar conexion") == -1) end_mem(0);
			continue;
		}
		int32_t* clientptr = malloc(sizeof(int32_t));
		*clientptr = client;
		pthread_t t;
		if( pthread_create(&t, NULL, serve_client, clientptr) != 0){
			if(printf_mutex(mutex_log, "Error al crear thrad") == -1) end_mem(0);
			end_mem(0);
		}
		if(pthread_detach(t) != 0) end_mem(0);
	}

}
