#ifndef GUTILS_H_
#define GUTILS_H_

#include<stdio.h>
#include<stdlib.h>
#include<signal.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netdb.h>
#include<string.h>
#include<commons/log.h>
#include<commons/config.h>
#include<commons/string.h>
#include<commons/temporal.h>
#include<readline/readline.h>
#include <readline/history.h>
#include<commons/collections/list.h>
#include<assert.h>
#include <pthread.h>
#include <semaphore.h> 
#include <math.h>
#include<commons/bitarray.h>
#include <sys/mman.h>
#include <dirent.h>




typedef struct{
	uint32_t size, offset;
	void *stream, *last;
} t_buff;

typedef enum {
	CLOSE = -1,
	PCB,
	PROGRAM,
	INSTRUCTION,
	INTERRUPT,
	STRING,
	ERROR,
	OK,
	DELETE_PROGRAM,
	NEWIO,
	INSTR_SEARCHER,
	FRAME_SEARCHER,
	FRAME,
	READ_BYTE_MEM,
	WRITE_BYTE_MEM,
	RESIZE
} op_code;

typedef struct{
	op_code code;
	t_buff* buff;
} t_pkg;

typedef struct{
	uint32_t value;
} t_struct_uint32;

typedef struct {
	uint32_t pc, eax, ebx, ecx, edx, si, di;
	uint8_t ax, bx ,cx, dx;
} t_cpucontext;


typedef struct {
    t_list* queue;
    pthread_mutex_t mutex;
} t_queue_mutex;

typedef struct {
	uint32_t pid, pc;
} t_instr_searcher;


typedef struct {
    void* value;
	int32_t size;
    pthread_mutex_t mutex;
} t_value_mutex;

typedef struct{
	uint32_t pid;
	t_queue_mutex* instrs_list;
} t_program;

typedef enum {
	T_EMPTY,
	T_IO,
	T_EXIT,
	T_ARITHEMTIC,
	T_RSC,
	T_ERR_OFM
} t_instr_type;
typedef enum {
	INT_EMPTY,
	INT_RR,
	INT_DELETE,
} t_intrr_reason;
typedef struct {
	t_instr_type type; //ARITHMETIC, IO, ... 
	t_intrr_reason intrr_reason;
	char* code;
	t_list* operands;
} t_instr;

typedef struct  {
    uint32_t pid, quantum;
	t_cpucontext* context;
	t_instr* instr;
} t_pcb;
typedef struct {
	char *name, *type;
	t_list* operations; 
	t_queue_mutex* pcb_queue;
	sem_t sem_io;
} t_io;
typedef struct {
	uint32_t pid;
	t_intrr_reason intrr_reason;
} t_interrupt;

typedef struct{
	int32_t instances;
	char* name;
	t_queue_mutex* blocked_pcbs;
	t_queue_mutex* owners_pcbs;
} t_rsc;

typedef struct {
	uint32_t pid, page_num, frame_num;
} t_tlb_entry;

typedef struct {
	uint32_t pid;
	t_list* page_frame_entries;
	int32_t last_accsd_page;
	uint32_t max_num_of_pages;
} t_page_table;

typedef struct {
	uint32_t pid, page_num;
} t_frame_searcher;



t_struct_uint32* struct_int_create(uint32_t value);
void struct_int_destroy(void* struct_int);


t_instr* instr_create(char*, t_instr_type, t_intrr_reason);
void instr_destroy(t_instr*);


//SOCKETS CLIENTE
int32_t crear_conexion(char*, char*);
void liberar_conexion(int32_t);

//SOCKETS SERVIDOR
int32_t start_servr(char*);

//buff
t_buff* buff_create(uint32_t);
void buff_destroy(t_buff*);

void buff_add(t_buff*, void*, uint32_t);
void buff_add_uint32(t_buff *, uint32_t);
void buff_add_uint8(t_buff*, uint8_t);
void buff_add_string(t_buff *, uint32_t, char *);
void buff_add_cpucontext(t_buff*, t_cpucontext*);
void buff_add_pcb(t_buff*, t_pcb*);
void buff_add_string_dynamic(t_buff*, char*);
void buff_add_instr(t_buff* ,t_instr*);

void buff_read(t_buff*, void*, uint32_t);
uint32_t buff_read_uint32(t_buff*);
uint8_t buff_read_uint8(t_buff*);
char* buff_read_string(t_buff*);
t_list* buff_read_list_string(t_buff*);
t_instr* buff_read_instr(t_buff*);
t_cpucontext* buff_read_cpucontext(t_buff*);
t_pcb* buff_read_pcb(t_buff*);


//pkg
t_pkg* pkg_create(op_code, t_buff*);
void pkg_destroy(t_pkg*);
void* pkg_serialize(t_pkg*, uint32_t);

int32_t send_pkg(t_pkg*, int32_t);

//RECEIVE
op_code recv_operacion(int32_t);
int32_t send_operacion(int32_t, op_code);

t_buff* recv_buff(int32_t);

//STRINGS SOCKET
int32_t send_string(int32_t, char*, op_code);
char* recv_string(int32_t);

//HANDSHAKES
int32_t handshake_cliente(int32_t, char*);
int32_t handshake_servidor(int32_t, char*);
int32_t connect_and_handshake(char*, char*, char*);

//CPU CONTEXT
t_cpucontext* create_cpucontext(void);
//PCB
t_pcb* pcb_create(uint32_t, t_instr*);
void pcb_destroy(void*);
int32_t pcb_send(int32_t, t_pcb*);
t_pcb* pcb_recv(int32_t );


t_instr* instr_create(char*, t_instr_type, t_intrr_reason);
void instr_destroy(t_instr*);


//MUTEX, QUEUE AND LIST
t_queue_mutex* create_queue_mutex(void);
void destroy_queue_mutex(t_queue_mutex*, void(*closure)(void*));
void destroy_only_queue_mutex(t_queue_mutex*);
void queue_mutex_push(t_queue_mutex*, void*);

void add_queue_mutex(t_queue_mutex*, void*);

void* pop_queue_mutex(t_queue_mutex*);
void* remove_last_queue_mutex(t_queue_mutex*);
uint32_t size_queue_mutex(t_queue_mutex*);
bool isempty_queue_mutex(t_queue_mutex* queue_mutex);
void iterate_queue_mutex(t_queue_mutex*, void(*closure)(void*));
void *find_queue_mutex(t_queue_mutex*, bool(*condition)(void*));
void *get_queue_index_mutex(t_queue_mutex*, uint32_t);
void* get_first_queue_mutex(t_queue_mutex*);
void set_first_queue_mutex(t_queue_mutex*, void*);
bool queue_mutex_remove_element(t_queue_mutex*, void*);
void remove_and_destroy_by_condition_queue_mutex(t_queue_mutex*, bool(*condition)(void*), void(*closure)(void*));


//MUTEX WITH A VALUE
t_value_mutex* create_value_mutex(int32_t);
void destroy_value_mutex(t_value_mutex*);
void assign_value_mutex(t_value_mutex*, void*);
void value_mutex_int_increase(t_value_mutex*);
void get_value_mutex(t_value_mutex*, void*);


int32_t send_uint32t(int32_t socket, uint32_t val, op_code code);
void* recv_uint32(int32_t, uint32_t*);

int32_t send2_uint32t(int32_t socket, uint32_t val, uint32_t val2, op_code code);
void* recv2_uint32(int32_t socket, uint32_t* val, uint32_t* val2);

//PATH AND PID
int32_t send_string_uint32t(int32_t, char*, uint32_t, op_code);
void* recv_string_uint32(int32_t, char**, uint32_t*);

//PROCESS IN MEM DATA TYPE
t_program* create_program(uint32_t);
void destroy_program(void* program);

t_io* io_create(char*, char*);
void io_destroy(void*);

int32_t send_pcb_string(int32_t, t_pcb *, char*);
t_pcb* recv_pcb_string(int32_t, char*);

t_instr_searcher* instr_searcher_create(uint32_t, uint32_t);
void instr_searcher_destroy(t_instr_searcher*);
int32_t send_instr_searcher(int32_t, t_instr_searcher*);
t_instr_searcher* recv_instr_seacrher(int32_t);

t_interrupt* interrupt_create(uint32_t pid, t_intrr_reason intrr_reason);
int32_t interrupt_send(int32_t socket, t_interrupt* intrr);
t_interrupt* interrupt_recv(int32_t socket);


t_page_table* page_table_create(uint32_t, uint32_t);
void page_table_destroy(void*);


t_frame_searcher* t_frame_searcher_create(uint32_t, uint32_t);
void t_frame_searcher_destroy(t_frame_searcher*);
int32_t t_frame_searcher_send(int32_t, t_frame_searcher*);
t_frame_searcher* t_frame_searcher_recv(int32_t);

int32_t send_write_byte_to_mem(uint32_t socket, void* data, uint32_t phdir, uint32_t pid);

t_tlb_entry* t_tlb_entry_create(uint32_t , uint32_t , uint32_t);
void t_tlb_entry_destroy(t_tlb_entry* );

char* string_concat(char* first_string, char* second_string);

t_rsc* rsc_create(int32_t instances , char* name);
void rsc_destroy(void*);

int32_t printf_mutex(pthread_mutex_t mutex, const char *message, ...);


#endif /* GUTILS_H_ */
