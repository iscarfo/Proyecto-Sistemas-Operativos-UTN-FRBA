#include "ker.h"

t_log* logger = NULL;
t_config* config = NULL;
t_config* path_config = NULL;

char *kerport, *kerpass, *alg_name=NULL, *test_path = NULL;
t_queue_mutex* queue_ready = NULL;
t_queue_mutex* queue_priority_ready = NULL;

t_queue_mutex* queue_new = NULL;
t_list* exit_list = NULL;
t_queue_mutex* exec_queue = NULL; //HAGO UN QUEUE PARA SIMPLIFICAR< EN REALIDAD ESTA QUE SOLO VA A TENER 1 ELEMENTO SIEMPRE

t_queue_mutex* queue_connected_ios = NULL;
t_queue_mutex* queue_resources = NULL;
uint32_t max_mupr = 0, quantum=0;
int32_t pid_counter = 1, err, cpuconn_dispatch=0, cpuconn_interrupt=0, memconn=0;

t_dictionary* console_commands_dictionary = NULL;
t_dictionary* alg_dictionary = NULL;
t_dictionary* rsc_op_dict = NULL;

sem_t plan_sem, rdy_procss_sem , waiting_procss_sem , opportunity_long_plan_sem ;

t_value_mutex* planning_mutex, *waiting_planification_processes;

pthread_mutex_t mutex_log;


void end_ker(int a){
	liberar_conexion(memconn);
	liberar_conexion(cpuconn_dispatch);
	liberar_conexion(cpuconn_interrupt);

	if(config != NULL) config_destroy(config);
	if(path_config != NULL) config_destroy(path_config);
	if(logger != NULL) log_destroy(logger);

	destroy_queue_mutex(queue_ready, &pcb_destroy);
	destroy_queue_mutex(queue_priority_ready, &pcb_destroy);
	destroy_queue_mutex(queue_new, &pcb_destroy);
	destroy_queue_mutex(exec_queue, &pcb_destroy);
	destroy_queue_mutex(queue_connected_ios, &io_destroy);
	destroy_queue_mutex(queue_resources, &rsc_destroy);

	list_destroy_and_destroy_elements(exit_list, &pcb_destroy);

	sem_destroy(&plan_sem);
	sem_destroy(&rdy_procss_sem);
	sem_destroy(&waiting_procss_sem);
	sem_destroy(&opportunity_long_plan_sem);

	pthread_mutex_destroy(&mutex_log);

	destroy_value_mutex(planning_mutex);

	if(rsc_op_dict != NULL) dictionary_destroy(rsc_op_dict);
	if(alg_dictionary != NULL) dictionary_destroy(alg_dictionary);
	if(console_commands_dictionary != NULL) dictionary_destroy(console_commands_dictionary);

	abort();
}
uint32_t get_current_mupr(void){
	int32_t mupr = 0;
	int i =0;
	t_io* io = NULL;
	if( size_queue_mutex(queue_connected_ios) != 0){
		while( (io=get_queue_index_mutex(queue_connected_ios, i))){
			mupr += size_queue_mutex(io->pcb_queue);
			++i;
		}
	} 
	i=0;
	t_rsc* rsc = NULL;
	while( (rsc=get_queue_index_mutex(queue_resources, i))){
		mupr += size_queue_mutex(rsc->blocked_pcbs);
		++i;
	} 

	mupr += size_queue_mutex(queue_ready)+size_queue_mutex(exec_queue);
	return mupr;
}


void modify_max_mupr(void* value) {
	if(value == NULL) return;
	
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "Grado de multiprogramacion actual %d", max_mupr);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	max_mupr = strtol(value, NULL, 10);
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "Nuevo grado de multiprogramacion es %d", max_mupr);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
	int32_t current_mupr = get_current_mupr();
	int new_num_of_processes = max_mupr-current_mupr;
	
	for(int i=0; i<new_num_of_processes; i++){
		if(sem_post(&opportunity_long_plan_sem) != 0) end_ker(0);
	}
}
void show_pid(void* pcb){
	if(pcb == NULL){ 
		if(printf_mutex(mutex_log, "pcb is null to show pid") == -1) end_ker(0); 
		return;
	}
	
	t_pcb* pcb_aux = pcb;
	
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "%d ", pcb_aux->pid);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0); 
}

void decode_command(char* code, char* data){
	void (*func)(void*) = NULL;
	func= dictionary_get(console_commands_dictionary, code);
	if(func == NULL){ 
		if(printf_mutex(mutex_log, "Command not found") == -1) end_ker(0); 
		return;
	}
	func(data);
}


void show_process_state(void*){
	if(printf_mutex(mutex_log, "Procesos en new:\n") == -1) end_ker(0); 
	iterate_queue_mutex(queue_new, show_pid);
	if(printf_mutex(mutex_log, "Procesos en ready:\n") == -1) end_ker(0); 
	iterate_queue_mutex(queue_ready, show_pid);
	if(printf_mutex(mutex_log, "Procesos en exit:\n") == -1) end_ker(0); 
	list_iterate(exit_list, show_pid);
	
	t_pcb* exec_pcb = NULL; exec_pcb = get_first_queue_mutex(exec_queue);
	if(exec_pcb != NULL){
		if(printf_mutex(mutex_log, "Proceso en ejecucion: %d \n", exec_pcb->pid) == -1) end_ker(0); 
	}
}

void start_process(void* path){
	// formato de path ./code/program1.txt"
	if(send_string_uint32t(memconn, path, pid_counter, PROGRAM) == -1) {
		if(printf_mutex(mutex_log, "Error al mandar path y pid del nuevo proceso") == -1) end_ker(0);  
		end_ker(0);

	}	
	t_pcb* new_pcb = NULL; new_pcb = pcb_create(pid_counter, NULL);

	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "Se crea el proceso %d en NEW \n", pid_counter);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	pid_counter++;

	add_queue_mutex(queue_new, new_pcb);

	if(get_current_mupr() < max_mupr){
		remove_last_queue_mutex(queue_new);
		add_queue_mutex(queue_ready, new_pcb);
		if(sem_post(&rdy_procss_sem) != 0) end_ker(0);

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: New - Estado Actual: Ready", new_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Cola Ready: ");
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		iterate_queue_mutex(queue_ready, show_pid);

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Cola Ready Prioridad: ");
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		iterate_queue_mutex(queue_priority_ready, show_pid);
		
	}
}
t_pcb* find_pcb_by_pid(t_queue_mutex* queue ,uint32_t pid){
	
	bool is_the_one(void* pcbptr){
		t_pcb* pcb = pcbptr;
		return  (pcb == NULL) ? false : (pcb->pid == pid);
	}
	
	return find_queue_mutex(queue, (void*)is_the_one);
}
void wait_rsc(void* vpcb, void* vrsc){
	t_rsc* rsc = vrsc; 
	t_pcb* pcb = vpcb;
	rsc->instances--;
	if(rsc->instances < 0){
		add_queue_mutex(rsc->blocked_pcbs, pcb);
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID %d - Bloqueado por %s", pcb->pid, rsc->name);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
		return; 
	}

	t_struct_uint32* pid = struct_int_create(pcb->pid);

	add_queue_mutex(rsc->owners_pcbs, pid);

	if(pcb->quantum < quantum && strcmp(alg_name, "VRR") == 0) queue_mutex_push(queue_priority_ready, pcb);
	else queue_mutex_push(queue_ready, pcb);

	if(sem_post(&rdy_procss_sem) != 0) end_ker(0);

	return;

}
void signal_rsc(void* vpcb, void* vrsc){
	t_rsc* rsc = NULL; rsc = vrsc; 
	t_pcb* pcb = NULL; pcb = vpcb;
	rsc->instances++;
	if(size_queue_mutex(rsc->blocked_pcbs) > 0){
		t_pcb* blocked_pcb = pop_queue_mutex(rsc->blocked_pcbs);

		t_struct_uint32* pid = struct_int_create(blocked_pcb->pid);
		add_queue_mutex(rsc->owners_pcbs, pid);

		add_queue_mutex(queue_ready, blocked_pcb);
		if(sem_post(&rdy_procss_sem) != 0) end_ker(0);
	}

	bool is_the_one(void* vstructint){	
					t_struct_uint32* struct_int = vstructint;
					return  struct_int->value == pcb->pid;
	}
	remove_and_destroy_by_condition_queue_mutex(rsc->owners_pcbs, is_the_one, free);

	if(pcb->quantum < quantum && strcmp(alg_name, "VRR") == 0) queue_mutex_push(queue_priority_ready, pcb);
	else queue_mutex_push(queue_ready, pcb);

	if(sem_post(&rdy_procss_sem) != 0) end_ker(0);
	return;
}

void release_process_rscs(t_pcb* pcb){
	t_rsc* rsc = NULL;
	int  i =0;
	t_struct_uint32* pid_owner = NULL;
	while( (rsc=get_queue_index_mutex(queue_resources, i))){
		if(queue_mutex_remove_element(rsc->blocked_pcbs, pcb) == true) {
			rsc->instances++;
		}

		bool is_the_one(void* vstructint){	
			t_struct_uint32* struct_int = vstructint;
			return  struct_int->value == pcb->pid;
		}

		pid_owner = find_queue_mutex(rsc->owners_pcbs, is_the_one);

		if(pid_owner != NULL){
			queue_mutex_remove_element(rsc->owners_pcbs, pid_owner);
			rsc->instances++;
			t_pcb* blocked_pcb = pop_queue_mutex(rsc->blocked_pcbs);
			add_queue_mutex(rsc->owners_pcbs, &(blocked_pcb->pid));
			add_queue_mutex(queue_ready, blocked_pcb);

			if(sem_post(&rdy_procss_sem) != 0) end_ker(0);
		}
		++i;
	}

}
void delete_process(void* vpcb){
	t_pcb* pcb = vpcb;
	t_pcb* xpcb = NULL; xpcb = get_first_queue_mutex(exec_queue); 
	if(xpcb != NULL && xpcb->pid == pcb->pid){
		t_interrupt delete; delete.pid = pcb->pid; delete.intrr_reason = INT_DELETE;
		interrupt_send(cpuconn_interrupt, &delete);
		return;
	}
	if(send_uint32t(memconn, pcb->pid, DELETE_PROGRAM) == -1) {
		if(printf_mutex(mutex_log, "Error al mandar path y pid del nuevo proceso") == -1) end_ker(0);  
		end_ker(0);
	}
	release_process_rscs(pcb);
	list_add(exit_list, pcb);
	if(sem_post(&opportunity_long_plan_sem) != 0) end_ker(0);
	
	if(pcb->instr->type == T_EXIT){ 
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Finaliza el proceso %d Motivo:Success\n", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	}
}

void find_pcb_to_delete(void* pidstring){
	int32_t pid = strtol(pidstring, NULL, 10);
	t_pcb* pcb_to_delete = NULL; 
	
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "Finaliza el proceso %d Motivo:Interrupted by user\n", pid);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);


	pcb_to_delete = find_pcb_by_pid(exec_queue, pid);
	if(pcb_to_delete != NULL){ 
		delete_process(pcb_to_delete);
		return;
	}
	pcb_to_delete = find_pcb_by_pid(queue_new, pid);
	if(pcb_to_delete != NULL){ 
		delete_process(pcb_to_delete);
		return;
	}

	pcb_to_delete = find_pcb_by_pid(queue_ready, pid);
	if(pcb_to_delete != NULL){ 
		delete_process(pcb_to_delete);
		return;
	}
	int i =0;
	t_io* io = NULL;
	while( (io=get_queue_index_mutex(queue_connected_ios, i)) ){
		if(io == NULL) break;
		pcb_to_delete = find_pcb_by_pid(io->pcb_queue, pid);
		if(pcb_to_delete != NULL) {
			delete_process(pcb_to_delete);
			break;
		}
		++i;
	}

	t_rsc* rsc = NULL;
	i =0;
	while( (rsc=get_queue_index_mutex(queue_resources, i))){
		if(rsc == NULL) break;
		pcb_to_delete = find_pcb_by_pid(rsc->blocked_pcbs, pid);
		if(pcb_to_delete != NULL) {
			delete_process(pcb_to_delete);
			break;
		}
		++i;
	}

}


void start_planification(void*) {
	bool value = false;
	get_value_mutex(planning_mutex, &value);
	if(!value){
		if(printf_mutex(mutex_log, "Activando planificacion..") == -1) end_ker(0); 
		value = true;
		assign_value_mutex(planning_mutex, &value);
		
		if(sem_post(&plan_sem) != 0) end_ker(0);
		

		uint32_t waiting = 0;
		get_value_mutex(waiting_planification_processes, &waiting);
		
		for(int i = 0; i<waiting; i++){
			if( sem_post(&waiting_procss_sem) != 0) end_ker(0);
		}

		waiting = 0;
		assign_value_mutex(waiting_planification_processes, &waiting);
		
		return;
	}
	if(printf_mutex(mutex_log, "Planificacion ya esta activa") == -1) end_ker(0); 	
}


void stop_planification(void*) {
	bool value = false;
	get_value_mutex(planning_mutex, &value);
	if(value){
		value = false;
		assign_value_mutex(planning_mutex, &value);
		if(printf_mutex(mutex_log, "Desactivando planificacion..") == -1) end_ker(0); 
		return;
	}
	if(printf_mutex(mutex_log, "Planificacion ya esta desactivada") == -1) end_ker(0); 	
}

// FUNCIONES POR CONSOLA         
void execute_script (void* path_script) {
	char* path = string_concat(test_path, path_script);
    FILE *file = fopen(path, "r");
	free(path);
    if (file == NULL) {
        if(printf_mutex(mutex_log, "Error al abrir el archivo de comandos") == -1) end_ker(0); 
        return;
    }
	
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Eliminar el salto de línea al final de la línea
        line[strcspn(line, "\n")] = 0;

        char** substrings = string_n_split(line, 2, " ");
        if (substrings[0] != NULL) {
            decode_command(substrings[0], substrings[1]);
        }

        string_array_destroy(substrings);
    }
    fclose(file);
}


void setup_console_commands_dictionary(void){
	console_commands_dictionary = dictionary_create();
	dictionary_put(console_commands_dictionary, "DETENER_PLANIFICACION", &stop_planification);
	dictionary_put(console_commands_dictionary, "INICIAR_PLANIFICACION", &start_planification);
	dictionary_put(console_commands_dictionary, "PROCESO_ESTADO", &show_process_state);
	dictionary_put(console_commands_dictionary, "EJECUTAR_SCRIPT", &execute_script);
	dictionary_put(console_commands_dictionary, "INICIAR_PROCESO", &start_process);	
	dictionary_put(console_commands_dictionary, "FINALIZAR_PROCESO", &find_pcb_to_delete);	
	dictionary_put(console_commands_dictionary, "MULTIPROGRAMACION", &modify_max_mupr);	
	dictionary_put(console_commands_dictionary, "END", &end_ker);
}

t_io* find_io_by_name(char* search_name){

	bool is_the_one(void* io){
		return  !(strcmp( ((t_io*)io)->name , search_name));
	}
	
	return find_queue_mutex(queue_connected_ios, (void*) is_the_one);
}


void* handle_io_request(t_pcb* pcb){
	char* io_name = list_get(pcb->instr->operands, 0);

	t_io* io = NULL; io = find_io_by_name(io_name);
	if(io == NULL){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Finaliza el proceso %d Motivo:Invalid interface\n", pcb->pid);
		log_info(logger, "PID: %d - Estado Anterior: Blocked - Estado Actual: Exit", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
		delete_process(pcb);
		return NULL;
	}
	
	char* op_to_search = pcb->instr->code;

	bool search (void* op_name){
		return !(strcmp((char*)op_name, op_to_search)); 
	}

	if ( list_any_satisfy(io->operations, &search) == false){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Finaliza el proceso %d Motivo:Invalid interface\n", pcb->pid);
		log_info(logger, "PID: %d - Estado Anterior: Blocked - Estado Actual: Exit", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(pcb);
		return NULL;
	}

	add_queue_mutex(io->pcb_queue, pcb);

	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "PID %d - Bloqueado por %s", pcb->pid, io->name);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);


	if(sem_post(&(io->sem_io)) != 0) end_ker(0);

	return ((void*)1);

}



void setup_rsc_op_dict(void){
	rsc_op_dict = dictionary_create();
	dictionary_put(rsc_op_dict, "WAIT", &wait_rsc);
	dictionary_put(rsc_op_dict, "SIGNAL", &signal_rsc);

}


t_rsc* find_rsc_by_name(char* name){

	bool is_the_one(void* rsc){
		return  !(strcmp( ((t_rsc*)rsc)->name , name));
	}
	
	return find_queue_mutex(queue_resources, (void*) is_the_one);
}
void* handle_rsc_request(t_pcb* pcb){
	char* rsc_name = list_get(pcb->instr->operands, 0);

	t_rsc* rsc = NULL; rsc = find_rsc_by_name(rsc_name);
	if(rsc == NULL){
		if(printf_mutex(mutex_log, "Recurso con ese nombre no existe") == -1) end_ker(0); 
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Finaliza el proceso %d Motivo:Invalid resource\n", pcb->pid);
		log_info(logger, "PID: %d - Estado Anterior: Blocked - Estado Actual: Exit", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(pcb);
		return NULL;
	}
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "Recurso: %s instancias %d", rsc->name, rsc->instances);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	void (*operation)(void*, void*) = NULL;
	operation= dictionary_get(rsc_op_dict, pcb->instr->code);
	if(operation == NULL){
		if(printf_mutex(mutex_log, "Esa opearcion no existe para los recursos") == -1) end_ker(0); 
		
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Finaliza el proceso %d Motivo:Invalid resource\n", pcb->pid);
		log_info(logger, "PID: %d - Estado Anterior: Blocked - Estado Actual: Exit", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(pcb);
		return NULL;
	}

	operation(pcb, rsc);
	return NULL;

}
	


void fifo (void){
	int32_t value;
	if(sem_getvalue(&rdy_procss_sem, &value) != 0) end_ker(0);
	if(!value){
		if(printf_mutex(mutex_log, "NO hay proceso ready para planificar\n") == -1) end_ker(0); 
	}
	if(sem_wait(&rdy_procss_sem) != 0) end_ker(0);

	set_first_queue_mutex(exec_queue, pop_queue_mutex(queue_ready));
	t_pcb* exec_pcb = NULL; exec_pcb = get_first_queue_mutex(exec_queue); 

	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "PID: %d - Estado Anterior: Ready - Estado Actual: Exec", exec_pcb->pid);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	if(pcb_send(cpuconn_dispatch, exec_pcb) == -1){
		if(printf_mutex(mutex_log, "Error al enviar el pcb del proceso a ejecutar\n") == -1) end_ker(0); 
		return;
	}
	
	
	recv_operacion(cpuconn_dispatch);
	t_pcb* updated_pcb = NULL; updated_pcb = pcb_recv(cpuconn_dispatch);
	if(updated_pcb == NULL){
		if(printf_mutex(mutex_log, "Ocurrio un error al recibir el pcb actualizado") == -1) end_ker(0); 
		return;
	}
	
	pcb_destroy(exec_pcb);
	set_first_queue_mutex(exec_queue, NULL);

	bool valuemutex = false;
	get_value_mutex(planning_mutex, &valuemutex);
	if(valuemutex == false){
		value_mutex_int_increase(waiting_planification_processes);
		if(sem_wait(&waiting_procss_sem) != 0) end_ker(0);
	}

	//HAGO UN IF DEL RETURN REASON PARA VER SI HAY QUE HACER IO o eliminar el proceso 
	if( updated_pcb->instr->type == T_EXIT || updated_pcb->instr->intrr_reason == INT_DELETE){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(updated_pcb);
		return;
	}  
	
	if(updated_pcb->instr->type == T_ERR_OFM){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		log_info(logger, "Finaliza el proceso %d - Motivo: OUT_OF_MEMORY", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
		
		delete_process(updated_pcb);
		return;
	}

	if( updated_pcb->instr->type == T_IO){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_io_request(updated_pcb);
		return;
	}
	if( updated_pcb->instr->type == T_RSC){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_rsc_request(updated_pcb);
		return;
	}			

}

void* quantum_rr(void* pidptr){
	usleep(quantum*1000);
	uint32_t* pid = pidptr;
	t_interrupt rr; rr.pid = *pid; rr.intrr_reason = INT_RR;
	interrupt_send(cpuconn_interrupt, &rr);
	return NULL;
}

void rr (void){
	int32_t value;
	if(sem_getvalue(&rdy_procss_sem, &value) != 0) end_ker(0);
	if(!value){
		if(printf_mutex(mutex_log, "NO hay proceso ready para planificar\n") == -1) end_ker(0); 
	}
	if(sem_wait(&rdy_procss_sem) != 0) end_ker(0);

	set_first_queue_mutex(exec_queue, pop_queue_mutex(queue_ready));
	t_pcb* exec_pcb = get_first_queue_mutex(exec_queue); 

	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "PID: %d - Estado Anterior: Ready - Estado Actual: Exec", exec_pcb->pid);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	if(pcb_send(cpuconn_dispatch, exec_pcb) == -1){
		if(printf_mutex(mutex_log, "Error al enviar el pcb del proceso a ejecutar\n") == -1) end_ker(0); 
	}
	pthread_t quantum_thread;
	pthread_create(&quantum_thread, NULL, quantum_rr, &(exec_pcb->pid));
	
	recv_operacion(cpuconn_dispatch);
	pthread_cancel(quantum_thread);
	t_pcb* updated_pcb = pcb_recv(cpuconn_dispatch);
	if(updated_pcb == NULL){
		if(printf_mutex(mutex_log, "Ocurrio un error al recibir el pcb actualizado") == -1) end_ker(0); 
		return;
	}
	
	pcb_destroy(exec_pcb);
	set_first_queue_mutex(exec_queue, NULL);

	bool valuemutex;
	get_value_mutex(planning_mutex, &valuemutex);
	if(valuemutex == false){
		value_mutex_int_increase(waiting_planification_processes);
		if(sem_wait(&waiting_procss_sem) != 0) end_ker(0);
	}

	//HAGO UN IF DEL RETURN REASON PARA VER SI HAY QUE HACER IO o eliminar el proceso 
	if( updated_pcb->instr->type == T_EXIT || updated_pcb->instr->intrr_reason == INT_DELETE){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(updated_pcb);
		return;
	} 
	if(updated_pcb->instr->type == T_ERR_OFM){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		log_info(logger, "Finaliza el proceso %d - Motivo: OUT_OF_MEMORY", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
		delete_process(updated_pcb);
		return;
	}

	if(updated_pcb->instr->intrr_reason == INT_RR){
		add_queue_mutex(queue_ready, updated_pcb);
		if(sem_post(&rdy_procss_sem) != 0) end_ker(0);

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Desalojado por fin de Quantum", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		return;
	}
	if( updated_pcb->instr->type == T_IO){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_io_request(updated_pcb);
		return;
	}
	if( updated_pcb->instr->type == T_RSC){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_rsc_request(updated_pcb);
		return;
	}		
}
typedef struct {
	uint32_t pid;
	int32_t quantum;
} temp_data_vrr;

void* quantum_vrr(void* datavrrv){
	temp_data_vrr* datavrr = datavrrv;
	if(datavrr->quantum > 0) usleep(1000* datavrr->quantum);
	t_interrupt rr; rr.pid = datavrr->pid; rr.intrr_reason = INT_RR;
	interrupt_send(cpuconn_interrupt, &rr);
	return NULL;
}
void vrr(void){
	int32_t value;
	if(sem_getvalue(&rdy_procss_sem, &value) != 0) end_ker(0);
	if(!value){
		if(printf_mutex(mutex_log, "NO hay proceso ready para planificar\n") == -1) end_ker(0); 
	}
	if(sem_wait(&rdy_procss_sem) != 0) end_ker(0);
	t_pcb* exec_pcb = NULL;

	if(size_queue_mutex(queue_priority_ready) == 0){
		set_first_queue_mutex(exec_queue, pop_queue_mutex(queue_ready));
		exec_pcb = get_first_queue_mutex(exec_queue);
		exec_pcb->quantum = quantum; 

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Ready - Estado Actual: Exec", exec_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
	}
	else{ 
		set_first_queue_mutex(exec_queue, pop_queue_mutex(queue_priority_ready));
		exec_pcb = get_first_queue_mutex(exec_queue); 
		
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Ready Priority - Estado Actual: Exec", exec_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
	}
	
	temp_data_vrr data_vrr; data_vrr.pid = exec_pcb->pid; data_vrr.quantum = exec_pcb->quantum;

	if(pcb_send(cpuconn_dispatch, exec_pcb) == -1){
		if(printf_mutex(mutex_log, "Error al enviar el pcb del proceso a ejecutar\n") == -1) end_ker(0); 
	}
	t_temporal* time_in_cpu = temporal_create();
	
	pthread_t quantum_thread;
	pthread_create(&quantum_thread, NULL, quantum_vrr, &(data_vrr));
	
	recv_operacion(cpuconn_dispatch);
	pthread_cancel(quantum_thread);

	temporal_stop(time_in_cpu);
	int64_t time = temporal_gettime(time_in_cpu);
	temporal_destroy(time_in_cpu);
	

	t_pcb* updated_pcb = pcb_recv(cpuconn_dispatch);
	if(updated_pcb == NULL){
		if(printf_mutex(mutex_log, "Ocurrio un error al recibir el pcb actualizado") == -1) end_ker(0); 
		return;
	}
	
	pcb_destroy(exec_pcb);
	set_first_queue_mutex(exec_queue, NULL);

	bool valuemutex = false;
	get_value_mutex(planning_mutex, &valuemutex);
	if(valuemutex == false){
		value_mutex_int_increase(waiting_planification_processes);
		if(sem_wait(&waiting_procss_sem) != 0) end_ker(0);
	}


	//HAGO UN IF DEL RETURN REASON PARA VER SI HAY QUE HACER IO o eliminar el proceso 
	if( updated_pcb->instr->type == T_EXIT || updated_pcb->instr->intrr_reason == INT_DELETE){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(updated_pcb);
		return;
	} 
	if(updated_pcb->instr->type == T_ERR_OFM){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Exit", updated_pcb->pid);
		log_info(logger, "Finaliza el proceso %d - Motivo: OUT_OF_MEMORY", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		delete_process(updated_pcb);
		return;
	}

	if(updated_pcb->instr->intrr_reason == INT_RR){
		add_queue_mutex(queue_ready, updated_pcb);
		if(sem_post(&rdy_procss_sem) != 0) end_ker(0);
		
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Desalojado por fin de Quantum", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		return;
	}

	//SOLO SI VOLVIO POR IO LE CAMBIO EL  QUANTUM VARIABLE
	updated_pcb->quantum -= time; 
	if( updated_pcb->instr->type == T_IO){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_io_request(updated_pcb);
		return;
	}
	if( updated_pcb->instr->type == T_RSC){
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Exec - Estado Actual: Blocked", updated_pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

		handle_rsc_request(updated_pcb);
		return;
	}			
}

void alg_dictionary_setup(void){
	alg_dictionary = dictionary_create();
	dictionary_put(alg_dictionary, "FIFO", &fifo);
	dictionary_put(alg_dictionary, "RR", &rr);
	dictionary_put(alg_dictionary, "VRR", &vrr);
}

void* short_plan(void* algorithm){
	void (*exec_alg)(void) = NULL;
	exec_alg = dictionary_get(alg_dictionary, alg_name);
	if(exec_alg == NULL){ if(printf_mutex(mutex_log, "Algoritmo no existe") == -1) end_ker(0);  end_ker(0);}  
	
	while(1){
		if(sem_wait(&plan_sem) != 0) end_ker(0);
		bool value;
		while(1){
			get_value_mutex(planning_mutex, &value);
			if(value) exec_alg();
			else break;
		}
	
	}
} 
void* long_plan(void*){
	while(1){
		if(sem_wait(&opportunity_long_plan_sem) != 0) end_ker(0);
		if(get_current_mupr() < max_mupr && size_queue_mutex(queue_new) != 0){
			add_queue_mutex(queue_ready, pop_queue_mutex(queue_new));
			if(sem_post(&rdy_procss_sem) != 0) end_ker(0);
			
			if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
			log_info(logger, "Cola Ready: ");
			if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

			iterate_queue_mutex(queue_ready, show_pid);
		}
	}
}




void* read_console(void* ptr){
	char* leido;
	while(1){
		leido = readline("\n>");
		if(leido){
			add_history(leido);
			if(!strncmp(leido, "exit", 4)) {
            	free(leido);
            	break;
        	}
			char** substrings = string_n_split(leido, 2, " ");
			free(leido);
			decode_command(substrings[0], substrings[1]);
			
			string_array_destroy(substrings);
		}
	}
	end_ker(0);
	return NULL;
}


t_io* recv_io_info(int32_t ioconn){
	t_buff* buff = NULL; buff = recv_buff(ioconn);
	if(buff == NULL){
		if(printf_mutex(mutex_log, "Error al recibir el buff de string y uint32") == -1) end_ker(0); 
		return NULL;
	}

	char* name = buff_read_string(buff);
	char* type = buff_read_string(buff);

	t_io* io = NULL; io = io_create(name, type);
	free(name); free(type);
	if(io == NULL){
		if(printf_mutex(mutex_log, "Crar io info error") == -1) end_ker(0); 
		return NULL;
	}
	

	list_destroy(io->operations);
	io->operations = buff_read_list_string(buff);

	buff_destroy(buff);
	return io;
}

void serve_io(int32_t ioconn){
	t_io* io = NULL; io = recv_io_info(ioconn);
	if(io == NULL){
		if(printf_mutex(mutex_log, "Error recibiendo io info ") == -1) end_ker(0); 
		return;
	}
	add_queue_mutex(queue_connected_ios, io);

	while(true){
		if(sem_wait(&io->sem_io) != 0) end_ker(0);

		t_pcb* pcb = pop_queue_mutex(io->pcb_queue);;

		if( pcb_send(ioconn, pcb) == -1){
			break;
		}

		if(recv_operacion(ioconn) == ERROR){
			if(printf_mutex(mutex_log, "Salio mal la io, la elimino toda") == -1) end_ker(0); 
			break;
		}
		
		//vuelve a ready
		bool value;
		get_value_mutex(planning_mutex, &value);
		if(value == false){
			value_mutex_int_increase(waiting_planification_processes);
			if(sem_wait(&waiting_procss_sem) != 0) end_ker(0);

		}
		if(pcb->quantum < quantum && strcmp(alg_name, "VRR") == 0) add_queue_mutex(queue_priority_ready, pcb);
		else add_queue_mutex(queue_ready, pcb);
		
		if(sem_post(&rdy_procss_sem) != 0) end_ker(0);

		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "PID: %d - Estado Anterior: Blocked - Estado Actual: Ready", pcb->pid);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
	}
	
	//se desonecta io
	queue_mutex_remove_element(queue_connected_ios, io);

	t_list_iterator* iterator = list_iterator_create(io->pcb_queue->queue);
	while(list_iterator_has_next(iterator)){
		delete_process(list_iterator_next(iterator));
	}
	list_iterator_destroy(iterator);
	
	io_destroy(io);

}

void* serve_client (void* vclient){
	int32_t* client = vclient;
	if(handshake_servidor(*client, kerpass) == -1){
		if(printf_mutex(mutex_log, "Handshake salio mal") == -1) end_ker(0); 
		return NULL;
	}
	int i = 1;
	do {
		switch (recv_operacion(*client)) {
			case CLOSE:
				if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
				log_info(logger, "El cliente se desconecto");
				if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
				--i;
			break;
			case STRING: //
				char* message_recvd = recv_string(*client);
				if( message_recvd == NULL){
					if(printf_mutex(mutex_log, "NO se puedo recibir el mensaje") == -1) end_ker(0); 
				}
				else{
					if(printf_mutex(mutex_log, "LLEGO %s", message_recvd) == -1) end_ker(0); 
				}
				free(message_recvd);
			break;
			case NEWIO: //
				serve_io(*client);
			break;
			default:
				log_warning(logger,"Operacion desconocida. No quieras meter la pata");
			break;
		}
	} while(i);
	free(client);
	return NULL;
}

void setup(void){
	setup_console_commands_dictionary();
	alg_dictionary_setup();
	planning_mutex = create_value_mutex(sizeof(bool));
	bool value = false;
	assign_value_mutex(planning_mutex, &value);

	waiting_planification_processes = create_value_mutex(sizeof(uint32_t));
	u_int32_t val = 0;
	assign_value_mutex(waiting_planification_processes, &val);

	if(sem_init(&plan_sem, 0, 0) != 0) end_ker(0);
	if(sem_init(&rdy_procss_sem, 0, 0) != 0) end_ker(0);
	if(sem_init(&waiting_procss_sem, 0, 0) != 0) end_ker(0);
	if(sem_init(&opportunity_long_plan_sem, 0, 0) != 0) end_ker(0);

	if( pthread_mutex_init(&mutex_log, NULL) != 0) end_ker(0);

	queue_ready = create_queue_mutex();
	if(queue_ready == NULL) end_ker(0);
	queue_priority_ready = create_queue_mutex();
	if(queue_priority_ready == NULL) end_ker(0); 
	queue_new = create_queue_mutex();
	if(queue_new == NULL) end_ker(0);
	exit_list = list_create();
	if(exit_list == NULL) end_ker(0);
	exec_queue = create_queue_mutex();
	if(exec_queue == NULL) end_ker(0);
	queue_connected_ios = create_queue_mutex();
	if(queue_connected_ios == NULL) end_ker(0);
	queue_resources = create_queue_mutex();
	if(queue_resources == NULL) end_ker(0);
	
	setup_rsc_op_dict();


	add_queue_mutex(exec_queue, NULL);

	

	/* ---------------- LOGGING ---------------- */
	logger = log_create("ker.log", "KERNEL", true, LOG_LEVEL_INFO);
	if(logger == NULL){
		if(printf_mutex(mutex_log, "Error logger KERNEL") == -1) end_ker(0); 
		end_ker(0);
	}
    /* ---------------- ARCHIVOS DE CONFIGURACION GENERAL---------------- */
	
	
	path_config = config_create("../general.config");
	if(path_config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_ker(0); 
		end_ker(0);
	}

	
}

int size_array(char** array){
	int i = 0;
	for (; i<99; i++){
		if(array[i] == NULL) break;
	}
	return i;
}

void fill_resources(void){
	char** rsc_name_array = config_get_array_value(config, "RECURSOS");
	char** rsc_instances_array = config_get_array_value(config, "INSTANCIAS_RECURSOS");

	int size = size_array(rsc_name_array);

	for(int i=0; i<size; i++){
		int32_t instances = strtol(rsc_instances_array[i], NULL, 10);
		t_rsc* rsc = rsc_create(instances, rsc_name_array[i]);
		add_queue_mutex(queue_resources, rsc);
	}
	for(int i =0;i<size; i++){
		t_rsc* rsc = get_queue_index_mutex(queue_resources, i);
		if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
		log_info(logger, "Nombre: %s , instancias: %d \n", rsc->name, rsc->instances);
		if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);
	}
	free(rsc_name_array); string_array_destroy(rsc_instances_array);
}

int main(int argc, char** argv){
	setup();

	config = config_create(argv[1]); // ejemplo ../generalfs.config
	if(config == NULL){
		if(printf_mutex(mutex_log, "Config Error MEMORY") == -1) end_ker(0); 
		end_ker(0);

	}

	fill_resources();
	char* ipmem = config_get_string_value(path_config, "IPMEM");
	char* memport = config_get_string_value(path_config, "MEMPORT");
	char* mempass= config_get_string_value(path_config, "MEMPASS");

	char* ipcpu = config_get_string_value(path_config, "IPCPU");
	char* cpu_dispatch_port = config_get_string_value(path_config, "CPU_PORT_DISPATCH");
	char* cpu_interrupt_port = config_get_string_value(path_config, "CPU_PORT_INTERRUPT");
	char* cpupass= config_get_string_value(path_config, "CPUPASS");

	kerport = config_get_string_value(path_config, "KERPORT");
	kerpass = config_get_string_value(path_config, "KERPASS");

	test_path = config_get_string_value(path_config, "TEST_PATH");

	quantum = config_get_int_value(config, "QUANTUM");
	alg_name = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
	if(pthread_mutex_lock(&mutex_log) != 0) end_ker(0);
	log_info(logger, "ALGORITMOS ES %s", alg_name);
	if(pthread_mutex_unlock(&mutex_log) != 0) end_ker(0);

	max_mupr = config_get_int_value(config, "GRADO_MULTIPROGRAMACION");

	



	/*---------------------- SOCKETS ----------------------------*/ 
	
	memconn = connect_and_handshake(ipmem, memport, mempass);
	if(memconn == 1){
		if(printf_mutex(mutex_log, "Error al conectar a mem") == -1) end_ker(0); 
		end_ker(0);
	}
	
	cpuconn_dispatch = connect_and_handshake(ipcpu, cpu_dispatch_port, cpupass);
	if(cpuconn_dispatch== 1){
		if(printf_mutex(mutex_log, "Error al conectar a cpu dispatch") == -1) end_ker(0); 
		end_ker(0);
	}
	
	cpuconn_interrupt = connect_and_handshake(ipcpu, cpu_interrupt_port, cpupass);
	if(cpuconn_interrupt == 1){
		if(printf_mutex(mutex_log, "Error al conectar a cpu interrupt") == -1) end_ker(0); 
		end_ker(0);
	}
	
	//SHORT PLAN
	pthread_t splan_thread;
	err = pthread_create(&splan_thread, NULL, short_plan, NULL);
	if(err != 0){
		if(printf_mutex(mutex_log, "Error al crear thread de planificacion") == -1) end_ker(0); 
		end_ker(0);
	}
	if(pthread_detach(splan_thread) != 0) end_ker(0);

	//LONG PLAN
	pthread_t lplan_thread;
	err = pthread_create(&lplan_thread, NULL, long_plan, NULL);
	if(err != 0){
		if(printf_mutex(mutex_log, "Error al crear thread de planificacion") == -1) end_ker(0); 
		end_ker(0);
	}
	if(pthread_detach(lplan_thread) != 0) end_ker(0);
	
	pthread_t console_thread;
	err = pthread_create(&console_thread, NULL, read_console, NULL);
	if(err != 0){
		if(printf_mutex(mutex_log, "Error al crear thread de consola") == -1) end_ker(0); 
		end_ker(0);
	}
	if(pthread_detach(console_thread) != 0) end_ker(0);
	

	int32_t server=0;
	server = start_servr(kerport);
	if(server == -1){
		if(printf_mutex(mutex_log, "Error al crear servidor kernel") == -1) end_ker(0); 
		end_ker(0);
		exit(1);
	}
	while(1){
		int32_t client = accept(server, NULL, NULL);
		if(client == -1){ 
			if(printf_mutex(mutex_log, "Error al aceptar conexion") == -1) end_ker(0); 
			continue;
		}
		int32_t* clientptr = malloc(sizeof(int32_t));
		*clientptr = client;
		pthread_t t;
		err = pthread_create(&t, NULL, serve_client , clientptr);
		if(err != 0){
			if(printf_mutex(mutex_log, "Error al crear thread de conexion") == -1) end_ker(0); 
			end_ker(0);
		}
		if(pthread_detach(t) != 0) end_ker(0);
	}

}



