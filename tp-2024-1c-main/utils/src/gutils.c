#include "gutils.h"

uint32_t code_err = -1;

//SOCKETS UTILS CLIENTE
//Crea conexion al servidor, devuelve -1 en errores
int32_t crear_conexion(char *ip, char* puerto){
	int32_t err;
	struct addrinfo hints, *server_info;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(ip, puerto, &hints, &server_info); 
	if(err!=0){ 
		printf("Error al obtener info de la red\n");
		freeaddrinfo(server_info);
		return -1;
	}
	int32_t socket_cliente = socket(server_info->ai_family, server_info->ai_socktype,server_info->ai_protocol);

	err = connect(socket_cliente, server_info->ai_addr, server_info->ai_addrlen);
	if(err == -1) {
		printf("Error al intentar conectar el cliente al server\n");
		freeaddrinfo(server_info);
		return err;
	}
	freeaddrinfo(server_info);

	return socket_cliente;
}

void liberar_conexion(int32_t socket_cliente){ 
	close(socket_cliente);
}


//Crea un socket de servidor y lo pone a escuchar en un puerto, devuelve -1 en errores
int32_t start_servr(char* port){
	int32_t socket_servidor, err;
	struct addrinfo hints, *servinfo;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(NULL, port, &hints, &servinfo);
	if(err != 0) {
		printf("Error al obtener info de la red\n");
		return code_err;
	}
	socket_servidor = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

	if (setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0){
    	printf("setsockopt(SO_REUSEADDR) failed");
		return code_err;
	}
	
	err = bind(socket_servidor, servinfo->ai_addr, servinfo->ai_addrlen);
	if(err != 0) {
		printf("Error al bindear el socket\n");
		return code_err;
	}

	err = listen(socket_servidor, SOMAXCONN); 
  	if(err == -1){
		printf("Error al setear en listen el socket\n");
		return code_err;
	}

	freeaddrinfo(servinfo);
	return socket_servidor;
}

//CPU CONTEXT
t_cpucontext* create_cpucontext(){
	t_cpucontext* cpucontext = malloc(sizeof(t_cpucontext));
	if(cpucontext == NULL){
		printf("Malloc error");
		return NULL;
	}
	cpucontext->ax = 0; cpucontext->bx = 0; cpucontext->cx= 0; cpucontext->dx = 0;
	cpucontext->di= 0; cpucontext->eax = 0; cpucontext->ebx= 0; cpucontext->ecx = 0; cpucontext->edx= 0;
	cpucontext->pc= 0; cpucontext->si= 0;
	return cpucontext;
}
uint32_t cpu_size(void){
	return 7*sizeof(uint32_t) + 4*sizeof(uint8_t);
}
void cpucontext_destroy(t_cpucontext* cpucontext){
	if(cpucontext != NULL) free(cpucontext);
}



//buff

t_struct_uint32* struct_int_create(uint32_t value){
	t_struct_uint32* str_int = malloc(sizeof(t_struct_uint32));
	if(str_int == NULL){
		printf("Malloc error");
		return NULL;
	}
	str_int->value = value;
	return str_int;
}

void struct_int_destroy(void* vstruct_int){
t_struct_uint32* struct_int = vstruct_int;
	if(struct_int == NULL) return;
	free(struct_int);
}

t_buff* buff_create(uint32_t size){
	t_buff* buff = malloc(sizeof(t_buff));
	if(buff == NULL){
		printf("Malloc error");
		return NULL;
	}
	buff->size = size;
	buff->offset = 0;
	buff->stream = calloc(size, 1);
	if(buff->stream == NULL){
		printf("Malloc error");
		return NULL;
	}
	buff->last = buff->stream + buff->size;
	return buff;
}
void buff_destroy(t_buff* buff){
	if(buff != NULL){
		free(buff->stream);
		free(buff);
	}
}

//buff ADD
//Agrega datos al buff que ocupan el size mandado y mueve el offset
void buff_add(t_buff* buff, void* data, uint32_t size){
	if((buff->stream + buff->offset) > buff->last ){
		printf("buff overflow");
		return;
	}
	memcpy((buff->stream+buff->offset), data, size);
	buff->offset += size;
}
// Agrega un uint32_t al buff
void buff_add_uint32(t_buff *buff, uint32_t data){
	buff_add(buff, &data, sizeof(data));
} 
// Agrega un uint8_t al buff
void buff_add_uint8(t_buff *buff, uint8_t data){
	buff_add(buff, &data, sizeof(data));
} 
// Agrega string al buff con un uint32_t atras indicando su longitud
void buff_add_string(t_buff *buff, uint32_t length, char *string){
	//Caracter /0 ya fue contado en el largo
	if(string == NULL) return;
	buff_add_uint32(buff, length);
	buff_add(buff, string, length);
}
void buff_add_cpucontext(t_buff* buff, t_cpucontext* cpucontext){
	if(cpucontext == NULL) return;
	buff_add_uint32(buff, cpucontext->pc);
	buff_add_uint32(buff, cpucontext->eax);
	buff_add_uint32(buff, cpucontext->ebx);
	buff_add_uint32(buff, cpucontext->ecx);
	buff_add_uint32(buff, cpucontext->edx);
	buff_add_uint32(buff, cpucontext->si);
	buff_add_uint32(buff, cpucontext->di);
	buff_add_uint8(buff, cpucontext->ax);
	buff_add_uint8(buff, cpucontext->bx);
	buff_add_uint8(buff, cpucontext->cx);
	buff_add_uint8(buff, cpucontext->dx);
}
void buff_add_string_dynamic(t_buff* buff, char* value){
	if(value == NULL) return;
	uint32_t size = strlen(value)+1;
	buff->stream = realloc(buff->stream, buff->size + size + sizeof(uint32_t));

	memcpy(buff->stream + buff->size, &size, sizeof(uint32_t));
	memcpy(buff->stream + buff->size + sizeof(uint32_t), value, size);

	buff->size += size + sizeof(uint32_t);
}
void buff_add_string_list(t_buff* buff, t_list* list){
	if(list == NULL || list_size(list) <= 0) return;
	t_list_iterator* iterator = list_iterator_create(list);
	while(list_iterator_has_next(iterator)){
		buff_add_string_dynamic(buff, list_iterator_next(iterator));
	}
	list_iterator_destroy(iterator);	
	
}
void buff_add_instr(t_buff* buff, t_instr* instr){
	if(instr == NULL) return;
	buff_add_uint32(buff, instr->type);
	buff_add_uint32(buff, instr->intrr_reason);

	if(instr->code != NULL) {
		buff_add_string_dynamic(buff, instr->code);
		buff_add_string_list(buff, instr->operands);
	}
}
void buff_add_pcb(t_buff* buff, t_pcb* pcb){
	if(pcb == NULL) return;
	buff_add_uint32(buff, pcb->pid);
	buff_add_uint32(buff, pcb->quantum);
	buff_add_cpucontext(buff, pcb->context);
	buff_add_instr(buff, pcb->instr);
}



//buff READ

//Copia el size de datos  mandado, desde el puntero buff, a una zona de memoria apuntada por el puntero data. Y desplaza el puntero buff lo que especifique el size
void buff_read(t_buff* buff, void* data, uint32_t data_size){
	if(buff->stream+buff->offset > buff->last){
		printf("buff overflow");
		return;
	}
	memcpy(data, (buff->stream+buff->offset), data_size);
	buff->offset += data_size;
}
// Lee un uint32_t del buff y avanza el offset
uint32_t buff_read_uint32(t_buff* buff){
	uint32_t data; 
	buff_read(buff, &data, sizeof(data));
	return data;
}
// Lee un uint8_t del buff y avanza el offset
uint8_t buff_read_uint8(t_buff* buff){
	uint8_t data; 
	buff_read(buff, &data, sizeof(data));
	return data;
}
// Lee un string y su longitud del buff y retorna el string
char* buff_read_string(t_buff* buff){
	uint32_t length = buff_read_uint32(buff);
	char* string = malloc(length);
	if(string == NULL){
		printf("Malloc error");
		return NULL;
	}
	buff_read(buff, string, length);
	return string;
}

t_list* buff_read_list_string(t_buff* buff){
	t_list* list = list_create();
	while(buff->offset < buff->size)		
		list_add(list, buff_read_string(buff));
	
	return list;
}
t_instr* buff_read_instr(t_buff* buff){
	uint32_t type = buff_read_uint32(buff); 
	uint32_t intrr_reason = buff_read_uint32(buff);
	
	if(buff->offset < buff->size){
		char* instr_code = buff_read_string(buff);
		t_instr* instr = instr_create(instr_code, type, intrr_reason);
		free(instr_code);

		list_destroy(instr->operands);
		instr->operands = buff_read_list_string(buff);
		return instr;
	}
	return NULL;
	
}
//Lee un contexto de cpu del buff y lo retorna
t_cpucontext* buff_read_cpucontext(t_buff*  buff){
	t_cpucontext* cpucontext = create_cpucontext();
	cpucontext->pc = buff_read_uint32(buff); 
	cpucontext->eax = buff_read_uint32(buff);
	cpucontext->ebx = buff_read_uint32(buff); 
	cpucontext->ecx = buff_read_uint32(buff); 
	cpucontext->edx = buff_read_uint32(buff);
	cpucontext->si = buff_read_uint32(buff);
	cpucontext->di = buff_read_uint32(buff);
	cpucontext->ax = buff_read_uint8(buff);
	cpucontext->bx = buff_read_uint8(buff);
	cpucontext->cx = buff_read_uint8(buff);
	cpucontext->dx = buff_read_uint8(buff);
	return cpucontext;
}
//Lee todos los datos necesarios para el pcb
t_pcb* buff_read_pcb(t_buff* buff){
	t_pcb* pcb = pcb_create( buff_read_uint32(buff), NULL);
	pcb->quantum = buff_read_uint32(buff);

	cpucontext_destroy(pcb->context);
	pcb->context = buff_read_cpucontext(buff);

	pcb->instr = buff_read_instr(buff);
	return pcb;
}


//pkg
t_pkg* pkg_create(op_code code, t_buff* buff){
	t_pkg* pkg = malloc(sizeof(t_pkg));
	if(pkg == NULL){
		printf("Malloc error");
		return NULL;
	}
	pkg->code = code;
	pkg->buff = buff;
	return pkg;
}

void pkg_destroy(t_pkg* pkg){
	if(pkg != NULL){
		buff_destroy(pkg->buff);
		free(pkg);
	}
}

void* pkg_serialize(t_pkg* pkg, uint32_t bytes){
	void* tosend = calloc(bytes, 1);
	if(tosend == NULL){
		printf("Mallor error");
		return NULL;
	}
	uint32_t offset = 0;
	
	memcpy(tosend, &(pkg->code), sizeof(pkg->code));
	offset += sizeof(pkg->code);
	memcpy(tosend+offset, &(pkg->buff->size), sizeof(pkg->buff->size));
	offset += sizeof(pkg->buff->size);
	memcpy(tosend+offset, pkg->buff->stream, pkg->buff->size);

	return tosend;
}
//COMUNICACION ENTRE MODULOS

//Envia el contenido del pkg serializado, devuelve -1 en errores 
int32_t send_pkg(t_pkg* pkg, int32_t socket){
	uint32_t bytes = pkg->buff->size + sizeof(pkg->buff->size) + sizeof(pkg->code);
	void* tosend = pkg_serialize(pkg, bytes);

	int32_t err = send(socket, tosend, bytes, 0);
	if(err == -1) printf("Error al enviar pkg");
	
	free(tosend);
	return err;
}

op_code recv_operacion(int32_t socket){
	op_code code;
	if(recv(socket, &code, sizeof(code), MSG_WAITALL) > 0)
		return code;
	
	else{
		close(socket);
		return -1;
	}
}

int32_t send_operacion(int32_t socket, op_code code){
	if( send(socket, &code, sizeof(code), 0) == -1){
		printf("Error sending op_code");
		return -1;
	}
	return 0;
}

//Crea un buff, recibe su size  y sus datos y los mete en el buff, devuelve NULL en errores
t_buff* recv_buff(int32_t socket){
	uint32_t size;
	if( recv(socket, &size, sizeof(size), MSG_WAITALL) == -1){
		printf("Error al recibir size del buff\n");
		return NULL;
	}
	t_buff* buff = buff_create(size);
	if(buff == NULL){
		printf("buff create error");
		return NULL;
	}
	if( recv(socket, buff->stream, size, MSG_WAITALL) == -1){
		printf("Error al recibir stream del buff\n");
		return NULL;
	}
	
	return buff;
}


//STRINGS SOCKETS
//Recibe el socket al que enviar el string y el mensaje, devuelve -1 en errores
int32_t send_string(int32_t socket, char* message, op_code code){
	uint32_t lenght = strlen(message)+1;
	
	t_buff* buff = buff_create(sizeof(uint32_t) + lenght);
	buff_add_string(buff, lenght, message);
	
	t_pkg* pkg = pkg_create(code, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	return err;
}
//Recibe un string del socket pasado por parametor, devuelve NULL en errores
char* recv_string(int32_t socket){
	t_buff* buff = recv_buff(socket);
	if (buff == NULL){
		printf("Error al recibir el buff de string\n");
		return NULL;
	}
	
	char* message = buff_read_string(buff);
	buff_destroy(buff);
	return message;
}

//HANDSHAKES
int32_t handshake_cliente(int32_t socket_server, char* password){
	int32_t err = send_string(socket_server, password, STRING);
	if( err == -1){
		printf("Error send handshake cliente\n");
		return err;
	}
	int32_t resultHandhsake;
	err = recv(socket_server, &resultHandhsake, sizeof(int32_t), MSG_WAITALL);
	if(err == -1){
		printf("Error recv handshake cliente\n");
		return err;
	}
	if(resultHandhsake != 0) printf("Clave handshake no correcta");

	return resultHandhsake;
}
//Hace un recv de la contrasenia del cliente y verifica si es correcta, devuelve -1 en errores, como parametro recibe el socket del cliente y la contrasenia que usa el servidor
int32_t handshake_servidor(int32_t socket_cliente, char * serverpassword){
	char* clientpassword;
	int32_t resultOk = 0, resultError = -1;
	recv_operacion(socket_cliente);
	clientpassword = recv_string(socket_cliente);
	if(clientpassword == NULL){
		printf("Error al recibir la contrasenia");
		return -1;
	}

	if(strcmp(clientpassword, serverpassword) == 0){
		send(socket_cliente, &resultOk, sizeof(int32_t), 0);
		free(clientpassword);
		return resultOk;
	}
	else{
		printf("Cliente NO tiene el protocolo correcto o password correcta");
		send(socket_cliente, &resultError, sizeof(int32_t), 0);
		free(clientpassword);
		return resultError;
	}
}
//CLiente crea la conexion y hace el handshake devuelve 1 en errores, devuelve la conexion al server
int32_t connect_and_handshake(char* ip, char* port, char* password){
	int32_t connection = crear_conexion(ip, port);
	if(connection == -1){
		printf("ERROR AL CREAR CONEXION");
		return 1;
	}
	
	if(handshake_cliente(connection, password) != 0){
		printf("Handshake salio mal");
		return 1;
	}
	return connection;
}






t_instr* instr_create(char* code, t_instr_type type, t_intrr_reason intrr_reason){
	t_instr* instr = malloc(sizeof(t_instr));
	if(instr == NULL){
		printf("Malloc error");
		return NULL;
	}
	uint32_t lenght = strlen(code)+1;
	instr->code = malloc(lenght);
	memcpy(instr->code, code, lenght);

	instr->operands = list_create();
	instr->type = type;
	instr->intrr_reason = intrr_reason;
	return instr;
}
uint32_t min_instr_size(void){
	return 2*sizeof(uint32_t); 
}
void instr_destroy(t_instr* instr){
	if(instr == NULL) return;
	free(instr->code);
	if(instr->operands != NULL) list_destroy_and_destroy_elements(instr->operands, free);
	free(instr);
}


t_pcb* pcb_create(uint32_t pid, t_instr* instr){
	t_pcb* pcb = malloc(sizeof(t_pcb));
	if(pcb == NULL){
		printf("Malloc error");
		return NULL;
	}
	pcb->pid = pid;
	pcb->quantum = 0;
	pcb->context = create_cpucontext();
	pcb->instr = instr;

	return pcb;
}
uint32_t pcb_size(void){
	return cpu_size() + min_instr_size() + 2*sizeof(uint32_t);
}

void pcb_destroy(void* pcbv){
	if(pcbv == NULL) return;
	t_pcb* pcb = pcbv;
	instr_destroy(pcb->instr);
	cpucontext_destroy(pcb->context);
	free(pcb);
}

//MUTEX,QUEUES AND LISTS

t_queue_mutex* create_queue_mutex(void) {
    t_queue_mutex* queue_mutex = malloc(sizeof(t_queue_mutex));
	if(queue_mutex == NULL){
		printf("Malloc error");
		return NULL;
	}
    queue_mutex->queue = list_create();

    if( pthread_mutex_init(&(queue_mutex->mutex), NULL) != 0) return NULL;
    
	return queue_mutex;
}
void destroy_queue_mutex(t_queue_mutex* queue_mutex, void(*element_destroyer)(void*)){
	if(queue_mutex == NULL || queue_mutex->queue == NULL || element_destroyer == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    list_destroy_and_destroy_elements(queue_mutex->queue, element_destroyer);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();

	pthread_mutex_destroy(&(queue_mutex->mutex));
	free(queue_mutex);
}
void destroy_only_queue_mutex(t_queue_mutex* queue_mutex){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    list_destroy(queue_mutex->queue);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();

	pthread_mutex_destroy(&(queue_mutex->mutex));
	free(queue_mutex);
}
void queue_mutex_push(t_queue_mutex* queue_mutex , void* info){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    list_add_in_index(queue_mutex->queue, 0, info);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
}
void add_queue_mutex(t_queue_mutex* queue_mutex , void* info){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
    if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    list_add(queue_mutex->queue, info);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
}
void* pop_queue_mutex(t_queue_mutex* queue_mutex ) {
	if(queue_mutex == NULL || queue_mutex->queue == NULL || size_queue_mutex(queue_mutex)==0) return NULL;
    if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    void* info = list_remove(queue_mutex->queue, 0);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
    return info;
}
void* remove_last_queue_mutex(t_queue_mutex* queue_mutex) {
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return NULL;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    void* info = list_remove(queue_mutex->queue, queue_mutex->queue->elements_count-1);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
    return info;
}
uint32_t size_queue_mutex(t_queue_mutex* queue_mutex){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return 0;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    uint32_t value = list_size(queue_mutex->queue);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	return value;
}
bool isempty_queue_mutex(t_queue_mutex* queue_mutex){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return true;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
    bool value = list_is_empty(queue_mutex->queue);
    if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	return value;
}
void iterate_queue_mutex(t_queue_mutex* queue_mutex, void(*func)(void*)){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	list_iterate(queue_mutex->queue, func);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
}
void* find_queue_mutex(t_queue_mutex* queue_mutex, bool(*condition)(void*)){
	if(queue_mutex == NULL || queue_mutex->queue == NULL || condition == NULL) return NULL;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	void* element = list_find(queue_mutex->queue, condition);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	return element;
}
void *get_queue_index_mutex(t_queue_mutex* queue_mutex, uint32_t index){
	if(queue_mutex == NULL || queue_mutex->queue == NULL || index>=size_queue_mutex(queue_mutex)) return NULL;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	void* element = list_get(queue_mutex->queue, index);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	return element;
}
void* get_first_queue_mutex(t_queue_mutex* queue_mutex){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return NULL;
	return get_queue_index_mutex(queue_mutex, 0);
}
void set_first_queue_mutex(t_queue_mutex* queue_mutex, void* element){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	list_replace(queue_mutex->queue, 0, element);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
}
bool queue_mutex_remove_element(t_queue_mutex* queue_mutex, void* element){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return false;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	bool result = list_remove_element(queue_mutex->queue, element);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	return result;
}
void queue_mutex_remove_by_condition(){}

void remove_and_destroy_by_condition_queue_mutex(t_queue_mutex* queue_mutex, bool(*condition)(void*), void(*closure)(void*)){
	if(queue_mutex == NULL || queue_mutex->queue == NULL) return;
	if(pthread_mutex_lock(&(queue_mutex->mutex)) != 0) abort();
	list_remove_and_destroy_by_condition(queue_mutex->queue, condition, closure);
	if(pthread_mutex_unlock(&(queue_mutex->mutex)) != 0) abort();
	
}

//VALUE MUTEX
//Recibe el size del value a guardar y devuelve el puntero
t_value_mutex* create_value_mutex(int32_t size){
	t_value_mutex* value_mutex = malloc(sizeof(t_value_mutex));
	if(value_mutex == NULL){
		printf("Malloc error");
		return NULL;
	}
	pthread_mutex_init(&(value_mutex->mutex), NULL);
	value_mutex->value = malloc(size);
	if(value_mutex->value == NULL){
		printf("Malloc error");
		return NULL;
	}
	value_mutex->size= size;
	return value_mutex;
}
void destroy_value_mutex(t_value_mutex* value_mutex){
	if(value_mutex == NULL) return;
	free(value_mutex->value);
	free(value_mutex);
}
void assign_value_mutex(t_value_mutex* value_mutex, void* value){
	if(pthread_mutex_lock(&(value_mutex->mutex)) != 0) abort();
	memcpy(value_mutex->value, value, value_mutex->size);
	if(pthread_mutex_unlock(&(value_mutex->mutex)) != 0) abort();
}
void value_mutex_int_increase(t_value_mutex* value_mutex){
	uint32_t current = 0;
	if(pthread_mutex_lock(&(value_mutex->mutex)) != 0) abort();
	memcpy(&current, value_mutex->value, value_mutex->size);
	current++;
	memcpy(value_mutex->value, &current, value_mutex->size);
	if(pthread_mutex_unlock(&(value_mutex->mutex)) != 0) abort();
}
//EL value del value mutex se guarda en el paramatero value que recibe
void get_value_mutex(t_value_mutex* value_mutex, void* value){
	if(pthread_mutex_lock(&(value_mutex->mutex)) != 0) abort();
	memcpy(value, value_mutex->value, value_mutex->size);
	if(pthread_mutex_unlock(&(value_mutex->mutex)) != 0) abort();
}


int32_t send_uint32t(int32_t socket, uint32_t val, op_code code){
	t_buff* buff = buff_create(sizeof(uint32_t));
	buff_add_uint32(buff, val);
	
	t_pkg* pkg = pkg_create(code, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	return err;
}
//Recibe el buff, lo lee y guarda en las variables que le pasamos lo leido, retorna NULL en errores
void* recv_uint32(int32_t socket, uint32_t* val){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de uint32");
		return NULL;
	}
	*val = buff_read_uint32(buff); 
	buff_destroy(buff);
	return ((void*)1);
}

int32_t send2_uint32t(int32_t socket, uint32_t val, uint32_t val2, op_code code){
	t_buff* buff = buff_create(2*sizeof(uint32_t));
	buff_add_uint32(buff, val);
	buff_add_uint32(buff, val2);
	
	t_pkg* pkg = pkg_create(code, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	return err;
}
//Recibe el buff, lo lee y guarda en las variables que le pasamos lo leido, retorna NULL en errores
void* recv2_uint32(int32_t socket, uint32_t* val, uint32_t* val2){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de uint32");
		return NULL;
	}
	*val = buff_read_uint32(buff); 
	*val2 = buff_read_uint32(buff);
	buff_destroy(buff);
	return ((void*)1);
}

//Manda un string y un uint, recibe el socket y los datos a mandar, devulve -1 en errores
int32_t send_string_uint32t(int32_t socket, char* path, uint32_t pid, op_code code){
	uint32_t lenght = strlen(path)+1;
	
	t_buff* buff = buff_create(sizeof(uint32_t)+lenght+sizeof(uint32_t) );
	buff_add_string(buff, lenght, path);
	buff_add_uint32(buff, pid);
	
	t_pkg* pkg = pkg_create(code, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	return err;
}
//Recibe el buff, lo lee y guarda en las variables que le pasamos lo leido, retorna NULL en errores
void* recv_string_uint32(int32_t socket, char** string, uint32_t* num){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de string y uint32");
		return NULL;
	}
	*string = buff_read_string(buff);
	*num = buff_read_uint32(buff); 
	buff_destroy(buff);
	return ((void*)1);
}

//PROGRAM IN MEM DATA TYPE
t_program* create_program(uint32_t pid){
    t_program* program = malloc(sizeof(t_program));
	if(program == NULL){
		printf("Malloc error");
		return NULL;
	}
    program->instrs_list = create_queue_mutex();
    program->pid = pid;
    return program;
}
void destroy_program(void* program){
	if(program == NULL) return;
	t_program* prog = program;
	destroy_queue_mutex(prog->instrs_list, free);
	free(prog);	
}

//io info fata type

t_io* io_create(char* name, char* type){
	t_io* io = malloc(sizeof(t_io));
	if(io == NULL){
		printf("Malloc error");
		return NULL;
	}
	uint32_t lenght = strlen(name)+1;
	io->name = malloc(lenght);
	memcpy(io->name, name, lenght);

	lenght = strlen(type)+1;
	io->type = malloc(lenght);
	memcpy(io->type, type, lenght);

	io->pcb_queue = create_queue_mutex();
	if(io->pcb_queue == NULL) return NULL;

	io->operations = list_create();

	sem_init(&(io->sem_io), 0, 0);
	return io;
}

void io_destroy(void* iov){
	if(iov == NULL) return;
	t_io* io = iov;
	free(io->name); free(io->type);
	destroy_queue_mutex(io->pcb_queue, (&pcb_destroy));
	list_destroy_and_destroy_elements(io->operations, free);
	sem_destroy(&(io->sem_io));
	free(io);
}



int32_t pcb_send(int32_t socket, t_pcb* pcb){
	if(pcb == NULL) return -1;
	
	t_buff* buff = buff_create(pcb_size());
	
	buff_add_pcb(buff, pcb); 

	t_pkg* pkg = pkg_create(PCB, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	
	return err;
}
t_pcb* pcb_recv(int32_t socket){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de pcb");
		return NULL;
	}
	t_pcb* pcb = buff_read_pcb(buff);
	
	buff_destroy(buff);
	return pcb;
}


//
t_instr_searcher* instr_searcher_create(uint32_t pid, uint32_t pc){
	t_instr_searcher* instr_searcher = malloc(sizeof(t_instr_searcher));
	if(instr_searcher == NULL){
		printf("Malloc error");
		return NULL;
	}
	instr_searcher->pid = pid;
	instr_searcher->pc = pc;

	return instr_searcher;
}
void instr_searcher_destroy(t_instr_searcher* instr_searcher){
	if(instr_searcher == NULL) return;
	free(instr_searcher);
}

int32_t send_instr_searcher(int32_t socket, t_instr_searcher* instr_searcher){
	t_buff* buff = buff_create(2*sizeof(uint32_t));
	buff_add_uint32(buff, instr_searcher->pid); buff_add_uint32(buff, instr_searcher->pc);

	t_pkg* pkg = pkg_create(INSTR_SEARCHER, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	
	return err;
}

t_instr_searcher* recv_instr_seacrher(int32_t socket){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de instr searcher");
		return NULL;
	}
	t_instr_searcher* instr_searcher = instr_searcher_create(0, 0);

	instr_searcher->pid = buff_read_uint32(buff);
	instr_searcher->pc = buff_read_uint32(buff);

	buff_destroy(buff);
	return instr_searcher;
}




t_interrupt* interrupt_create(uint32_t pid, t_intrr_reason intrr_reason){
	t_interrupt* intrr = malloc(sizeof(t_interrupt));
	if(intrr == NULL){
		printf("Malloc error");
		return NULL;
	}
	intrr->pid = pid;
	intrr->intrr_reason = intrr_reason;
	return intrr;
}

int32_t interrupt_send(int32_t socket, t_interrupt* intrr){
	t_buff* buff = buff_create(sizeof(intrr->pid)+sizeof(uint32_t));

	buff_add_uint32(buff, intrr->pid); buff_add_uint32(buff, intrr->intrr_reason);

	t_pkg* pkg = pkg_create(INTERRUPT, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	
	return err;
}

t_interrupt* interrupt_recv(int32_t socket){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de instr searcher");
		return NULL;
	}
	t_interrupt* intrr = interrupt_create(0,0);
	intrr->pid = buff_read_uint32(buff);
	intrr->intrr_reason = buff_read_uint32(buff);

	buff_destroy(buff);
	return intrr;
}

t_page_table* page_table_create(uint32_t max_num_of_pages, uint32_t pid){
	t_page_table* page_table = malloc(sizeof(*page_table));
	if(page_table == NULL){
		printf("Malloc error");
		return NULL;
	}
	page_table->pid = pid;
	page_table->page_frame_entries = list_create();
	if(page_table->page_frame_entries == NULL){
		printf("Malloc error");
		return NULL;
	}
	page_table->last_accsd_page = -1;
	page_table->max_num_of_pages = max_num_of_pages;
	return page_table;
}
void page_table_destroy(void* page_tablev){
	if(page_tablev == NULL) return;
	t_page_table* page_table = page_tablev;
	list_destroy_and_destroy_elements(page_table->page_frame_entries, free);
	free(page_table);
	
}

t_frame_searcher* t_frame_searcher_create(uint32_t pid, uint32_t page_num){
	t_frame_searcher* frame_searcher = malloc(sizeof(*frame_searcher));
	if(frame_searcher == NULL){
		printf("Malloc error");
		return NULL;
	}
	frame_searcher->pid = pid;
	frame_searcher->page_num = page_num;
	return frame_searcher;
}
void t_frame_searcher_destroy(t_frame_searcher* frame_searcher){
	if(frame_searcher != NULL) free(frame_searcher);
}

int32_t t_frame_searcher_send(int32_t socket, t_frame_searcher* frame_searcher){
	t_buff* buff = buff_create(2*sizeof(uint32_t));
	buff_add_uint32(buff, frame_searcher->pid); buff_add_uint32(buff, frame_searcher->page_num);

	t_pkg* pkg = pkg_create(FRAME_SEARCHER, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	
	return err;
}

t_frame_searcher* t_frame_searcher_recv(int32_t socket){
	t_buff* buff = recv_buff(socket);
	if(buff == NULL){
		printf("Error al recibir el buff de t_frame_searcher");
		return NULL;
	}
	t_frame_searcher* frame_searcher = t_frame_searcher_create(0, 0);

	frame_searcher->pid = buff_read_uint32(buff);
	frame_searcher->page_num = buff_read_uint32(buff);

	buff_destroy(buff);
	return frame_searcher;
}

int32_t send_write_byte_to_mem(uint32_t socket,void* data, uint32_t phdir, uint32_t pid){
	t_buff* buff = buff_create(2*sizeof(uint32_t)+sizeof(char));
	buff_add_uint32(buff, phdir);
	buff_add_uint32(buff, pid);
	buff_add(buff, data, sizeof(char));
	
	t_pkg* pkg = pkg_create(WRITE_BYTE_MEM, buff);
	int32_t err = send_pkg(pkg, socket);
	if(err == -1) printf("Error al enviar el uint32");
	pkg_destroy(pkg);
	return err;
}


t_tlb_entry* t_tlb_entry_create(uint32_t pid, uint32_t page_num, uint32_t frame_num){
	t_tlb_entry* tlb_entry = malloc(sizeof(t_tlb_entry));
	if(tlb_entry == NULL) return NULL;
	tlb_entry->pid = pid; tlb_entry->page_num = page_num; tlb_entry->frame_num = frame_num;
	return tlb_entry;
} 
void t_tlb_entry_destroy(t_tlb_entry* tlb_entry){
	if(tlb_entry != NULL) free(tlb_entry);
}


char* string_concat(char* first_string, char* second_string){
	uint32_t length1 = strlen(first_string);
	uint32_t length2 = strlen(second_string)+1;

	uint32_t final_length = length1+length2;

	char* final = malloc(final_length);
	
	memcpy(final, first_string, length1);
	memcpy(final+length1, second_string, length2);
	return final;
}

void rsc_destroy(void* rscv){
	if(rscv == NULL) return;
	t_rsc* rsc = rscv;
	destroy_queue_mutex(rsc->blocked_pcbs, (&pcb_destroy));
	destroy_queue_mutex(rsc->owners_pcbs, &struct_int_destroy);
	free(rsc->name);
	free(rsc);
}
t_rsc* rsc_create(int32_t instances , char* name){
	t_rsc* rsc = malloc(sizeof(t_rsc));
	if(rsc == NULL){
		printf("Malloc error");
		return NULL;
	}
	rsc->instances = instances;
	rsc->name = name;
	rsc->blocked_pcbs = create_queue_mutex();
	if(rsc->blocked_pcbs == NULL){
		rsc_destroy(rsc);
		return NULL;
	}
	 
	rsc->owners_pcbs = create_queue_mutex();
	if(rsc->owners_pcbs == NULL){
		rsc_destroy(rsc);
		return NULL;
	}
	return rsc;
}



int32_t printf_mutex(pthread_mutex_t mutex, const char *message, ...){
	va_list argp;
    va_start(argp, message);

	if(pthread_mutex_lock(&mutex) != 0) return -1;
	vprintf(message, argp);
	if(pthread_mutex_unlock(&mutex) != 0) return -1;
	va_end(argp);

	return 0;
}
