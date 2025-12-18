#include "cpu.h"

int32_t memconn = 0, err = 0;
t_log* logger = NULL;
t_config* config = NULL;
t_config* path_config = NULL;

char  *cpu_dispatch_port, *cpu_interrupt_port, *cpupass;

uint32_t entries_tlb = 0;
uint32_t page_size = 0;

t_dictionary* instr_dictionary, *register_dictionary, *sizeof_register_dictionary;
t_queue_mutex* queue_interrupt;

t_value_mutex* pid_to_interrupt;

sem_t is_intrr;

t_tlb_entry** tlb = NULL; 
void (*handle_hit_alg)(uint32_t) = NULL; 
void (*tlb_alg)(t_tlb_entry*) = NULL;

pthread_mutex_t mutex_log;


void end_cpu(int a){
	liberar_conexion(memconn);
	if(config != NULL) config_destroy(config);
	if(path_config != NULL) config_destroy(path_config);
	
	if(logger != NULL) log_destroy(logger);

	if(instr_dictionary != NULL) dictionary_destroy(instr_dictionary);
	if(register_dictionary != NULL) dictionary_destroy(register_dictionary);
	if(sizeof_register_dictionary != NULL) dictionary_destroy(sizeof_register_dictionary);

	destroy_queue_mutex(queue_interrupt, free);

	destroy_value_mutex(pid_to_interrupt);

	sem_destroy(&is_intrr);

	for(int i = 0; i< entries_tlb; i++){
		t_tlb_entry_destroy(tlb[i]);
	}
	free(tlb);

	pthread_mutex_destroy(&mutex_log);

	abort();
}



int32_t frame_num_get(uint32_t page_num, uint32_t pid){
	if(entries_tlb != 0){
		for(int i=0; i< entries_tlb; i++){
			if(tlb != NULL && tlb[i] != NULL && tlb[i]->pid == pid && tlb[i]->page_num == page_num){
				if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
				log_info(logger, "PID: %d -TLB HIT-Pagina: %d", pid, page_num);
				if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

				uint32_t frame_num = tlb[i]->frame_num; 
				handle_hit_alg(i);
				return frame_num;
			} 
		}
	}
	if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
	log_info(logger, "PID: %d -TLB MISS -Pagina: %d", pid, page_num);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

	t_frame_searcher frame_searcher; frame_searcher.pid=pid; frame_searcher.page_num=page_num;

	if(t_frame_searcher_send(memconn, &frame_searcher) == -1){
		end_cpu(0);
	}

	if(recv_operacion(memconn) != FRAME){
		if(printf_mutex(mutex_log, "Error al recbir operacion instruccion") == -1) end_cpu(0);
		return -1;
	}
	uint32_t frame = 0;
	if( recv_uint32(memconn, &frame) == NULL){
		if(printf_mutex(mutex_log, "Error al recibir el frame num") == -1) end_cpu(0);
		return -1;
	}
	if(entries_tlb != 0){
		t_tlb_entry* new_tlb_entry = t_tlb_entry_create(pid, page_num, frame);
		tlb_alg(new_tlb_entry);
	}

	return frame;
}

void tlb_fifo(t_tlb_entry* tlb_entry){
	if(entries_tlb == 0) return;
	for(int i=0; i<entries_tlb; i++){
		if(tlb[i] == NULL){
			tlb[i] = tlb_entry;
			return;
		}
	}
	t_tlb_entry_destroy(tlb[0]); tlb[0] = NULL;
	for(int i=0; i<entries_tlb-1; i++){
		tlb[i] = tlb[i+1];
	}
	tlb[entries_tlb-1] = tlb_entry;
}
void handle_hit_fifo(uint32_t){}

void tlb_lru(t_tlb_entry* tlb_entry){
	if(entries_tlb == 0) return;
	for(int i=0; i< entries_tlb; i++){
		if(tlb[i] == NULL){
			tlb[i] = tlb_entry;
			return;
		}
	}

	t_tlb_entry_destroy(tlb[0]);tlb[0] = NULL;
	for(int i=0; i<entries_tlb-1; i++){
		tlb[i] = tlb[i+1];
	}
	tlb[entries_tlb-1] = tlb_entry;
}

void handle_hit_lru(uint32_t index){
	uint32_t max_index = entries_tlb-1;
	if(entries_tlb == 0) return;
	if(index<max_index && tlb[index+1] == NULL) return;
	if(index == max_index) return;
	
	t_tlb_entry* hitted_tlb = tlb[index];
	for(int i=index; i<max_index; i++){
		tlb[i] = tlb[i+1];
	}
	tlb[max_index] = NULL;
	
	for(int i=max_index-1; i>= 0; i--){
		if(tlb[i] != NULL){
			tlb[i+1] = hitted_tlb;
			break;
		}
	}
}

uint32_t mmu(uint32_t ldir, uint32_t pid){
	uint32_t page_num = floor(ldir / page_size );
	uint32_t offset = ldir - page_num * page_size;

	if( offset/page_size >= 1){
		uint32_t amount = floor(offset/page_size);
		page_num += amount;
		offset -= page_size*amount;
	}

	int32_t frame = frame_num_get(page_num, pid);
	if(frame == -1){
		//ERROR
	}
	uint32_t phdir = frame*page_size + offset;

	if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
	log_info(logger, "PID: %d - OBTENER MARCO - Página: %d - Marco: %d", pid, page_num, frame);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);
	return phdir;
}

int32_t get_register_value(char* reg_name, t_pcb* pcb){
	void* (*get_register)(t_pcb*) = dictionary_get(register_dictionary, reg_name);
	if(get_register == NULL){
		if(printf_mutex(mutex_log, "%s register does not exist", reg_name) == -1) end_cpu(0);
		return -1;
	}
	void* regist = get_register(pcb);

	uint32_t value = 0;
	if( dictionary_get(sizeof_register_dictionary, reg_name) == NULL){
		value = *(uint8_t*)regist;
	}
	else {
		value = *(uint32_t*)regist;
	}

	return value;
}

void modify_register_value(char* reg_name, t_pcb* pcb){

}

void* SET(t_pcb* pcb){
	char* reg_name = list_get(pcb->instr->operands, 0);
	uint32_t value = strtol(list_get(pcb->instr->operands, 1), NULL, 10);

	void* (*get_register)(t_pcb*) = dictionary_get(register_dictionary, reg_name);
	if(get_register == NULL){
		if(printf_mutex(mutex_log, "%s register does not exist", reg_name) == -1) end_cpu(0);
		return NULL;
	}
	void* registr = get_register(pcb);
	
	
	if( dictionary_get(sizeof_register_dictionary, reg_name) == NULL){
		*(uint8_t*)registr = value;
	}
	else {
		*(uint32_t*)registr = value;
	}

	return ((void*)1);
    
}
void* MOV_IN(t_pcb* pcb) {
	char* data_reg = list_get(pcb->instr->operands, 0);

	void* (*get_register)(t_pcb*) = dictionary_get(register_dictionary, data_reg);
	if(get_register == NULL){
		if(printf_mutex(mutex_log, "%s register does not exist", data_reg) == -1) end_cpu(0);
		return NULL;
	}
	void* data_register = get_register(pcb);

	char* addres_reg = list_get(pcb->instr->operands, 1);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	if( dictionary_get(sizeof_register_dictionary, data_reg) == NULL){
		uint32_t phdir = mmu(ldir, pcb->pid);
		
		if(send2_uint32t(memconn, phdir, pcb->pid, READ_BYTE_MEM) == -1){
			if(printf_mutex(mutex_log, "error al mandar direccion fisicaa leer") == -1) end_cpu(0); 
			return NULL;
		}

		recv(memconn, data_register, 1, MSG_WAITALL);
		if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
		log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %d", pcb->pid, phdir, *(uint8_t*) data_register);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);
	}
	else {
		for(int i=0; i<4; i++){
			uint32_t phdir = mmu(ldir+i, pcb->pid);
			if(send2_uint32t(memconn, phdir, pcb->pid, READ_BYTE_MEM) == -1){
				if(printf_mutex(mutex_log, "error al mandar direccion fisicaa leer") == -1) end_cpu(0); 
				return NULL;
			}
			recv(memconn, data_register+i, 1, MSG_WAITALL);
			
			if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
			log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %d", pcb->pid, phdir, *(uint8_t*) (data_register+i));
			if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);
		}
	}


	return ((void*)1);
}

void* COPY_STRING(t_pcb* pcb){
	uint32_t ldir_to_copy = pcb->context->si;
	uint32_t ldir_to_paste = pcb->context->di;
	uint32_t bytes = strtol(list_get(pcb->instr->operands, 0), NULL, 10);
	char* character = calloc(1, 1);
	if(character == NULL){
		return NULL;
	}

	for(int i = 0; i<bytes; i++){
		uint32_t phdir_copy = mmu(ldir_to_copy+i, pcb->pid);
		uint32_t phdir_paste = mmu(ldir_to_paste+i, pcb->pid);
		
		if(send2_uint32t(memconn, phdir_copy, pcb->pid, READ_BYTE_MEM) == -1){
			if(printf_mutex(mutex_log, "error al mandar direccion fisicaa leer") == -1) end_cpu(0); 
			return NULL;
		}
		recv(memconn, character, 1, MSG_WAITALL);

		if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
		log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %c", pcb->pid, phdir_copy, *(char*)character);
		log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %c", pcb->pid, phdir_paste, *(char*)character);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

		send_write_byte_to_mem(memconn, character, phdir_paste, pcb->pid);
	}	
	free(character);
	return NULL;
}



void* MOV_OUT(t_pcb* pcb) {
	char* data_reg = list_get(pcb->instr->operands, 1);

	void* (*get_register)(t_pcb*) = dictionary_get(register_dictionary, data_reg);
	if(get_register == NULL){
		if(printf_mutex(mutex_log, "%s register does not exist", data_reg) == -1) end_cpu(0);
		return NULL;
	}
	void* data_register = get_register(pcb);

	char* addres_reg = list_get(pcb->instr->operands, 0);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	if( dictionary_get(sizeof_register_dictionary, data_reg) == NULL){
		uint32_t phdir = mmu(ldir, pcb->pid);

		if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
		log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %d", pcb->pid, phdir, *(uint8_t*) data_register );
		if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

		send_write_byte_to_mem(memconn, data_register, phdir, pcb->pid);

	}
	else {
		for(int i=0; i<4; i++){
			uint32_t phdir = mmu(ldir+i, pcb->pid);
			if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
			log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %d", pcb->pid, phdir, *(uint8_t*) (data_register+i) );
			if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

			send_write_byte_to_mem(memconn, data_register+i, phdir, pcb->pid);
			
		}
	}
	return NULL;
}

void* SUM(t_pcb* pcb) {
	char* reg1_name = list_get(pcb->instr->operands, 0);
	void* (*get_register1)(t_pcb*) = dictionary_get(register_dictionary, reg1_name);
	
	if( get_register1 == NULL){
		if(printf_mutex(mutex_log, "one of the registers does not exist") == -1) end_cpu(0);
		return NULL;
	}
	void* registr1 = get_register1(pcb);
	
	char* reg2_name = list_get(pcb->instr->operands, 1);
	uint32_t result = get_register_value(reg2_name, pcb);

	if( dictionary_get(sizeof_register_dictionary, reg1_name) == NULL){
		*(uint8_t*)registr1 += result;
	}
	else {
		*(uint32_t*)registr1 += result;
	}
	return ((void*)1);
}

void* SUB(t_pcb* pcb) {
   	char* reg1_name = list_get(pcb->instr->operands, 0);
	void* (*get_register1)(t_pcb*) = dictionary_get(register_dictionary, reg1_name);
	
	if( get_register1 == NULL){
		if(printf_mutex(mutex_log, "one of the registers does not exist") == -1) end_cpu(0);
		return NULL;
	}
	void* registr1 = get_register1(pcb);
	
	char* reg2_name = list_get(pcb->instr->operands, 1);
	uint32_t result = get_register_value(reg2_name, pcb);

	if( dictionary_get(sizeof_register_dictionary, reg1_name) == NULL){
		*(uint8_t*)registr1 -= result;
	}
	else {
		*(uint32_t*)registr1 -= result;
	}
	return ((void*)1);
}
void* JNZ(t_pcb* pcb) {
	char* reg_name = list_get(pcb->instr->operands, 0);
	void* (*get_register1)(t_pcb*) = dictionary_get(register_dictionary, reg_name);
	if( get_register1 == NULL){
		if(printf_mutex(mutex_log, "Register does not exist") == -1) end_cpu(0);
		return NULL;
	}
	
	uint32_t newpc = strtol(list_get(pcb->instr->operands, 1), NULL, 10);
	//le resto 1 para balancear que en el ciclo de instruccion se suma 1
	newpc--;
	
	void* registr1 = get_register1(pcb);


	if( (dictionary_get(sizeof_register_dictionary, reg_name) == NULL &&  *(uint8_t*)registr1 != 0) ||  (dictionary_get(sizeof_register_dictionary, reg_name) == (void*)1 &&  *(uint32_t*)registr1 != 0) ){
		pcb->context->pc = newpc;
	}
	return ((void*)1);
}

void* RESIZEf(t_pcb* pcb){
	uint32_t bytes = strtol(list_get(pcb->instr->operands, 0), NULL, 10);

	if(send2_uint32t(memconn, bytes, pcb->pid, RESIZE) == -1){
		pcb->instr->type = T_EXIT;
		return ((void*)1);
	}
	uint32_t result=0;
	if( recv(memconn, &result, sizeof(uint32_t), MSG_WAITALL) == -1){
		pcb->instr->type = T_EXIT;
		return ((void*)1);
	}
	if(result == 0){
		return ((void*)1);
	}
	else{
		pcb->instr->type = T_ERR_OFM;
		return ((void*)1);
	}

}
uint32_t str_length_ints = 10;
void* IO_STDIN_READ(t_pcb* pcb) { 
	char* addres_reg = list_get(pcb->instr->operands, 1);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	char* size_reg = list_get(pcb->instr->operands, 2);
	uint32_t size = get_register_value(size_reg, pcb);
	free(size_reg);
	
	char* strsize = malloc(str_length_ints);
	sprintf(strsize, "%u", size);
	list_replace(pcb->instr->operands, 2, strsize);

	for (int i = 0; i<size; i++){
		uint32_t phdir = mmu(ldir+i, pcb->pid);
		char* strphdir = malloc(str_length_ints);
		sprintf(strphdir, "%u", phdir);
		list_add(pcb->instr->operands, strphdir);
	}


	pcb->instr->type = T_IO;
	return ((void*)1);
}
void* IO_STDOUT_WRITE(t_pcb* pcb) { 
	char* addres_reg = list_get(pcb->instr->operands, 1);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	char* size_reg = list_get(pcb->instr->operands, 2);
	uint32_t size = get_register_value(size_reg, pcb);
	free(size_reg);

	char* strsize = malloc(str_length_ints);
	sprintf(strsize, "%u", size);
	list_replace(pcb->instr->operands, 2, strsize);

	for (int i = 0; i<size; i++){
		uint32_t phdir = mmu(ldir+i, pcb->pid);
		char* strphdir = malloc(str_length_ints);
		sprintf(strphdir, "%u", phdir);
		list_add(pcb->instr->operands, strphdir);
	}

	pcb->instr->type = T_IO;
	return ((void*)1);
}

void* IO_FS_CREATE(t_pcb* pcb) {
	pcb->instr->type = T_IO;
	return ((void*)1);
}

void* IO_FS_DELETE(t_pcb* pcb){
	pcb->instr->type = T_IO;
	return ((void*)1);
}


void* IO_FS_TRUNCATE(t_pcb* pcb) {
	char* size_reg = list_get(pcb->instr->operands, 2);
	uint32_t size = get_register_value(size_reg, pcb);
	free(size_reg);

	char* strsize = malloc(str_length_ints);
	sprintf(strsize, "%u", size);

	list_replace(pcb->instr->operands, 2, strsize);

	pcb->instr->type = T_IO;
	return ((void*)1);
}


void* IO_FS_WRITE(t_pcb* pcb) {
	char* addres_reg = list_get(pcb->instr->operands, 2);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	char* size_reg = list_get(pcb->instr->operands, 3);
	uint32_t size_in_bytes = get_register_value(size_reg, pcb);
	free(size_reg);
	char* strsize = malloc(str_length_ints);
	sprintf(strsize, "%u", size_in_bytes);
	list_replace(pcb->instr->operands, 3, strsize);

	char* ptr_reg = list_get(pcb->instr->operands, 4);
	uint32_t ptr_file = get_register_value(ptr_reg, pcb);
	free(ptr_reg);
	char* strptr = malloc(str_length_ints);
	sprintf(strptr, "%u", ptr_file);
	list_replace(pcb->instr->operands, 4, strptr);

	for (int i = 0; i<size_in_bytes; i++){
		uint32_t phdir = mmu(ldir+i, pcb->pid);
		char* strphdir = malloc(str_length_ints);
		sprintf(strphdir, "%u", phdir);
		list_add(pcb->instr->operands, strphdir);
	}

	pcb->instr->type = T_IO;
	return ((void*)1);
}


void* IO_FS_READ(t_pcb* pcb) {
	char* addres_reg = list_get(pcb->instr->operands, 2);
	uint32_t ldir = get_register_value(addres_reg, pcb);

	char* size_reg = list_get(pcb->instr->operands, 3);
	uint32_t size_bytes = get_register_value(size_reg, pcb);
	free(size_reg);
	char* strsize = malloc(str_length_ints);
	sprintf(strsize, "%u", size_bytes);
	list_replace(pcb->instr->operands, 3, strsize);

	char* ptr_reg = list_get(pcb->instr->operands, 4);
	uint32_t ptr_file = get_register_value(ptr_reg, pcb);
	free(ptr_reg);
	char* strptr = malloc(str_length_ints);
	sprintf(strptr, "%u", ptr_file);
	list_replace(pcb->instr->operands, 4, strptr);

	for (int i = 0; i<size_bytes; i++){
		uint32_t phdir = mmu(ldir+i, pcb->pid);
		char* strphdir = malloc(str_length_ints);
		sprintf(strphdir, "%u", phdir);
		list_add(pcb->instr->operands, strphdir);
	}

	pcb->instr->type = T_IO;
	return ((void*)1);
}


void* IO_GEN_SLEEP(t_pcb* pcb) {
	pcb->instr->type = T_IO;
	return ((void*)1);

}
void* WAIT(t_pcb* pcb){
	pcb->instr->type = T_RSC;
	return ((void*)1);
}
void* SIGNAL(t_pcb* pcb){
	pcb->instr->type = T_RSC;
	return ((void*)1);
}
void* EXIT(t_pcb* pcb){
	pcb->instr->type = T_EXIT;
	return ((void*)1);
}

void setup_instrs_dictionary(){
	instr_dictionary = dictionary_create();
	dictionary_put(instr_dictionary, "SET", &SET);
	dictionary_put(instr_dictionary, "MOV_IN", &MOV_IN);
	dictionary_put(instr_dictionary, "MOV_OUT", &MOV_OUT);
	dictionary_put(instr_dictionary, "SUM", &SUM);
	dictionary_put(instr_dictionary, "SUB", &SUB);
	dictionary_put(instr_dictionary, "JNZ", &JNZ);
	dictionary_put(instr_dictionary, "RESIZE", &RESIZEf);
	dictionary_put(instr_dictionary, "COPY_STRING", &COPY_STRING);
	dictionary_put(instr_dictionary, "WAIT", &WAIT);
	dictionary_put(instr_dictionary, "SIGNAL", &SIGNAL);
	dictionary_put(instr_dictionary, "IO_GEN_SLEEP", &IO_GEN_SLEEP);
	dictionary_put(instr_dictionary, "IO_STDIN_READ", &IO_STDIN_READ);
	dictionary_put(instr_dictionary, "IO_STDOUT_WRITE", &IO_STDOUT_WRITE);
	dictionary_put(instr_dictionary, "IO_FS_CREATE", &IO_FS_CREATE);
	dictionary_put(instr_dictionary, "IO_FS_DELETE", &IO_FS_DELETE);
	dictionary_put(instr_dictionary, "IO_FS_TRUNCATE", &IO_FS_TRUNCATE);
	dictionary_put(instr_dictionary, "IO_FS_WRITE", &IO_FS_WRITE);
	dictionary_put(instr_dictionary, "IO_FS_READ", &IO_FS_READ);
	dictionary_put(instr_dictionary, "EXIT", &EXIT);	
}
uint8_t* get_ax(t_pcb* pcb){
	return &(pcb->context->ax);
}
uint8_t* get_bx(t_pcb* pcb){
	return &(pcb->context->bx);
}
uint8_t* get_cx(t_pcb* pcb){
	return &(pcb->context->cx);
}
uint8_t* get_dx(t_pcb* pcb){
	return &(pcb->context->dx);
}
uint32_t* get_pc(t_pcb* pcb){
	return &(pcb->context->pc);
}
uint32_t* get_eax(t_pcb* pcb){
	return &(pcb->context->eax);
}
uint32_t* get_ebx(t_pcb* pcb){
	return &(pcb->context->ebx);
}
uint32_t* get_ecx(t_pcb* pcb){
	return &(pcb->context->ecx);
}
uint32_t* get_edx(t_pcb* pcb){
	return &(pcb->context->edx);
}
uint32_t* get_si(t_pcb* pcb){
	return &(pcb->context->si);
}
uint32_t* get_di(t_pcb* pcb){
	return &(pcb->context->di);
}
void setup_register_dictionary(){
	register_dictionary = dictionary_create();
	dictionary_put(register_dictionary, "AX", &get_ax);
	dictionary_put(register_dictionary, "BX", &get_bx);
	dictionary_put(register_dictionary, "CX", &get_cx);
	dictionary_put(register_dictionary, "DX", &get_dx);

	dictionary_put(register_dictionary, "PC", &get_pc);	
	
	dictionary_put(register_dictionary, "EAX", &get_eax);
	dictionary_put(register_dictionary, "EBX", &get_ebx);
	dictionary_put(register_dictionary, "ECX", &get_ecx);
	dictionary_put(register_dictionary, "EDX", &get_edx);
	
	dictionary_put(register_dictionary, "SI", &get_si);
	dictionary_put(register_dictionary, "DI", &get_di);

}
void setup_sizeregister_dictionary(){
	sizeof_register_dictionary = dictionary_create();
	void* sizebyte = NULL; void* sizedoubleword = (void*)1;
	dictionary_put(sizeof_register_dictionary, "AX", sizebyte);
	dictionary_put(sizeof_register_dictionary, "BX", sizebyte);
	dictionary_put(sizeof_register_dictionary, "CX", sizebyte);
	dictionary_put(sizeof_register_dictionary, "DX", sizebyte);

	dictionary_put(sizeof_register_dictionary, "PC", sizedoubleword);	
	
	dictionary_put(sizeof_register_dictionary, "EAX", sizedoubleword);
	dictionary_put(sizeof_register_dictionary, "EBX", sizedoubleword);
	dictionary_put(sizeof_register_dictionary, "ECX", sizedoubleword);
	dictionary_put(sizeof_register_dictionary, "EDX", sizedoubleword);
	
	dictionary_put(sizeof_register_dictionary, "SI", sizedoubleword);
	dictionary_put(sizeof_register_dictionary, "DI", sizedoubleword);
}


void start_interrupt_server(void){
	int32_t server_interrupt = start_servr(cpu_interrupt_port);
	if(server_interrupt == -1){
		if(printf_mutex(mutex_log, "Error al crear servidor interrupt") == -1) end_cpu(0);
		end_cpu(0);
	}
	
	while(1){
		int32_t client_intrr = accept(server_interrupt, NULL, NULL);
		if(client_intrr == -1){ 
			if(printf_mutex(mutex_log, "Error al aceptar conexion") == -1) end_cpu(0);
			continue;
		}
		if(handshake_servidor(client_intrr, cpupass) == -1){
			if(printf_mutex(mutex_log, "Error al hacer handshake con cliente") == -1) end_cpu(0);
			continue;;
		}

		bool cycle = true;
		do{
			switch (recv_operacion(client_intrr) ){
				case PCB:
				break;
				case INTERRUPT: 
					t_interrupt* interrupt = interrupt_recv(client_intrr);
					if(interrupt == NULL){
						if(printf_mutex(mutex_log, "Error al recbir ") == -1) end_cpu(0);
						break;
					}
					add_queue_mutex(queue_interrupt, interrupt);
					sem_post(&is_intrr);

					
				break;
				case CLOSE:
					if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
					log_info(logger, "El cliente se desconecto interrupt");
					if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

					cycle = false;
				break;
				default:
					log_warning(logger,"Operacion desconocida. No quieras meter la pata interrupt");
				break;
			}
		} while(cycle);
	}
}

void* instr_cycle(t_pcb* pcb){
	while(1){
		//FETCH
		if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
		log_info(logger, "PID: %d - FETCH - PROGRAM COUNTER: %d \n", pcb->pid, pcb->context->pc);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

		t_instr_searcher* instr_searcher = instr_searcher_create(pcb->pid, pcb->context->pc);
		if(send_instr_searcher(memconn, instr_searcher) == -1){
			if(printf_mutex(mutex_log, "Error al enviar el instr searcher a memoria para obtener instruccion") == -1) end_cpu(0);
			return NULL;
		}
		instr_searcher_destroy(instr_searcher);

		if( recv_operacion(memconn) != INSTRUCTION){
			if(printf_mutex(mutex_log, "Error al recbir operacion instruccion") == -1) end_cpu(0); 
			return NULL;
		}
		char* instr_str = recv_string(memconn);
		if(instr_str == NULL){
			if(printf_mutex(mutex_log, "Error receiving instr") == -1) end_cpu(0); 
			return NULL;
		}
		//DECODE
		char** substrings = string_split(instr_str, " ");

		t_instr* instr = instr_create(substrings[0], T_EMPTY, INT_EMPTY);
		int32_t i = 1;
		while(substrings[i] != NULL){
			uint32_t lenght = strlen(substrings[i])+1;
			char* operand = malloc(lenght);
			memcpy(operand, substrings[i], lenght);
			list_add(instr->operands, operand);
			i++;
		}

		string_array_destroy(substrings);
		
		
		void* (*func)(void*) = dictionary_get(instr_dictionary, instr->code);
		if(func == NULL){
			if(printf_mutex(mutex_log, "Instruccion no existe error") == -1) end_cpu(0);
			return NULL;
		}
		if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
		log_info(logger, "PID %d Ejecutando %s", pcb->pid, instr_str);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);

		free(instr_str);

		//EXECUTE
		pcb->instr = instr;
		
		func(pcb);
		pcb->context->pc++;

		//HAGO IFS DE RETUNR REASON PARA VER SI ES POR UN EXIT O POR IO
		
		
		if( pcb->instr->type == T_EXIT || pcb->instr->type == T_IO || pcb->instr->type == T_ERR_OFM || pcb->instr->type == T_RSC) 
			return ((void*)1);

		//CHECK INTERRUPT
		int32_t val;
		sem_getvalue(&is_intrr, &val);
		if(val > 0){
			sem_wait(&is_intrr);
			t_interrupt* interrupt = pop_queue_mutex(queue_interrupt);
			if(interrupt->pid == pcb->pid){
				//atiendo la interrupcion
				pcb->instr->intrr_reason = interrupt->intrr_reason;

				free(interrupt);
				return ((void*)1);
			}
			free(interrupt);
		}
		instr_destroy(instr);
		pcb->instr = NULL;
	}
}

void* start_dispatch_server(void*){
	int32_t server_dispatch = start_servr(cpu_dispatch_port);
	if(server_dispatch == -1){
		if(printf_mutex(mutex_log, "Error al crear servidor dispatch") == -1) end_cpu(0);
		end_cpu(0);
	}
	while(1){
		int32_t clientconn_dispatch = accept(server_dispatch, NULL, NULL);
		if(clientconn_dispatch == -1){ 
			if(printf_mutex(mutex_log, "Error al aceptar conexion") == -1) end_cpu(0);
			continue;
		}
		if(handshake_servidor(clientconn_dispatch, cpupass) == -1){
			if(printf_mutex(mutex_log, "Error al hacer handshake con cliente") == -1) end_cpu(0);
			continue;
		}

		bool cycle = true;
		do {
			switch ( recv_operacion(clientconn_dispatch) ) {
				case CLOSE:
					if(pthread_mutex_lock(&mutex_log) != 0) end_cpu(0);
					log_info(logger, "El cliente se desconecto dispatch");
					if(pthread_mutex_unlock(&mutex_log) != 0) end_cpu(0);
					
					cycle = false;
				break;
				case PCB:
					t_pcb* pcb = pcb_recv(clientconn_dispatch);
					if(pcb == NULL){
						if(printf_mutex(mutex_log, "Error al recibir el pcb a procesar") == -1) end_cpu(0);
						break;
					}
					instr_destroy(pcb->instr);
					
					if(instr_cycle(pcb) == NULL){
						if(printf_mutex(mutex_log, "Error en el ciclo de instruccion") == -1) end_cpu(0); 
						break;
					}
					pcb_send(clientconn_dispatch, pcb);
					
					pcb_destroy(pcb);

				break;
				case STRING:
					char* message_recvd = recv_string(clientconn_dispatch);
					if(message_recvd == NULL){
						if(printf_mutex(mutex_log, "NO se puedo recibir el mensaje dispatch\n") == -1) end_cpu(0);
					}
					else{ 
						if(printf_mutex(mutex_log, "LLEGO %s", message_recvd) == -1) end_cpu(0);
					}
					free(message_recvd);
				break;
				default:
					log_warning(logger,"Operacion desconocida. No quieras meter la pata dispatch");
				break;
			}
		} while(cycle);
	}
}


int main(int argc, char** argv){
	signal(SIGINT, &end_cpu);
	if( pthread_mutex_init(&mutex_log, NULL) != 0) end_cpu(0);
	queue_interrupt = create_queue_mutex();
	sem_init(&is_intrr, 0, 0);
	pid_to_interrupt = create_value_mutex(sizeof(int32_t));
	int32_t val = -1;
	assign_value_mutex(pid_to_interrupt, &val);

	setup_sizeregister_dictionary();
	setup_register_dictionary();
	setup_instrs_dictionary();
	
	/* ---------------- LOGGING ---------------- */
	
	t_log_level info = LOG_LEVEL_INFO;
	logger = log_create("cpu.log", "CPU", true, info);
	if(logger == NULL){
		if(printf_mutex(mutex_log, "Error logger CPU") == -1) end_cpu(0);
		end_cpu(0);
	}
	/* ---------------- ARCHIVOS DE CONFIGURACION ---------------- */
	
	path_config = config_create("../general.config");
	if(path_config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_cpu(0);
		end_cpu(0);
	}

	char* ipmem = config_get_string_value(path_config, "IPMEM");
	char* memport = config_get_string_value(path_config, "MEMPORT");
	char* mempass= config_get_string_value(path_config, "MEMPASS");

	cpupass = config_get_string_value(path_config, "CPUPASS");
	
	cpu_dispatch_port = config_get_string_value(path_config, "CPU_PORT_DISPATCH");
	cpu_interrupt_port = config_get_string_value(path_config, "CPU_PORT_INTERRUPT");

	config = config_create(argv[1]); // ejemplo ../generalfs.config
	if(config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_cpu(0);
		end_cpu(0);

	}

	entries_tlb = config_get_int_value(config, "ENTRIES_TLB");
	page_size = config_get_int_value(config, "PAGE_SIZE");

	char* tlb_alg_name = config_get_string_value(config, "ALG_TLB");
	if(strcmp(tlb_alg_name, "FIFO") == 0){
		handle_hit_alg = &handle_hit_fifo;
		tlb_alg = &tlb_fifo;
	}
	else {
		handle_hit_alg = &handle_hit_lru;
		tlb_alg = &tlb_lru;
	}

	tlb = calloc(entries_tlb, sizeof(t_tlb_entry*)); 
	if(tlb == NULL){ 
		if(printf_mutex(mutex_log,"Malloc error") == -1) end_cpu(0);
		end_cpu(0);
	}

	//CREAR CONEXION DE CLIENTE DEL CPU A MEM
	memconn = connect_and_handshake(ipmem, memport, mempass);
	if(memconn == 1){
		if(printf_mutex(mutex_log, "Error al crear conexion a memoria") == -1) end_cpu(0);
		end_cpu(0);
	}

	//CREAR FUNCIONALIDAD SERVER DEL CPU
	//DISPATCH SERVER
	pthread_t dispatch_thread;
	if( pthread_create(&dispatch_thread, NULL, start_dispatch_server, NULL) !=0 ){
		if(printf_mutex(mutex_log, "Problema al crear thread dispatch") == -1) end_cpu(0);
		end_cpu(0);
	}
	if(pthread_detach(dispatch_thread) != 0) end_cpu(0);

	//INTERRUPT SERVER
	
	start_interrupt_server();
}


