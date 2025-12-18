# Proyecto-Sistemas-Operativos-UTN-FRBA

## Integrantes
- Ignacio Scarfo
- Luca Trias Lupinacci
- Santiago Nicolás Torres Franco

## 📝 Descripción General

Este proyecto fue desarrollado para la cátedra de **Sistemas Operativos** y consiste en el diseño e implementación de un **sistema distribuido** capaz de simular las funciones principales de un sistema operativo real.

El sistema utiliza una **arquitectura modular**, permitiendo:
- Gestión de procesos
- Administración de memoria mediante **paginación**
- Resolución de operaciones de entrada/salida a través de un sistema de archivos propio llamado **DialFS**

---

## 🏛️ Arquitectura del Sistema

El sistema se divide en **cuatro módulos independientes** que se comunican entre sí mediante **sockets TCP/IP**:

- **Kernel:** Responsable de la planificación de procesos y la gestión de recursos.
- **CPU:** Simula el ciclo de instrucción y la traducción de direcciones lógicas a físicas (MMU / TLB).
- **Memoria:** Administra el espacio de usuario y las tablas de páginas.
- **Interfaz de I/O:** Representa los dispositivos periféricos que interactúan con el sistema.

---

## ⚙️ Componentes Detallados

### 1. Kernel – Planificación y Estados

El Kernel gestiona el ciclo de vida de los procesos utilizando un **modelo de cinco estados**.  
Soporta los siguientes **algoritmos de planificación**:

- FIFO
- Round Robin (RR)
- Virtual Round Robin (VRR)

**Funcionalidades principales:**
- **Multiprogramación:** Control dinámico del grado de multiprogramación.
- **Gestión de Recursos:** Implementación de semáforos mediante las instrucciones `WAIT` y `SIGNAL`.

---

### 2. CPU – Ciclo de Instrucción

La CPU interpreta el pseudocódigo simulando el comportamiento de un hardware real.

**Características:**
- Registros de propósito general: `AX`, `BX`, `CX`, `DX` (1 byte) y versiones extendidas de 4 bytes.
- Etapas del ciclo de instrucción:
  - Fetch
  - Decode
  - Execute
  - Check Interrupt
- **MMU & TLB:** Implementación de una TLB con algoritmos FIFO o LRU para optimizar el acceso a memoria.

---

### 3. Memoria y FileSystem – DialFS

La memoria implementa un esquema de **paginación simple**, donde el tamaño total de la memoria es siempre múltiplo del tamaño de página.

**DialFS – Sistema de Archivos**
- Asignación contigua de bloques.
- Uso de un **Bitmap** para el control de bloques libres.
- **Compactación:** Ante fragmentación externa, el sistema compacta archivos para generar espacio contiguo disponible.

---

## 📅 Cronograma de Desarrollo

El proyecto se desarrolló siguiendo una **metodología iterativa incremental**, dividido en tres hitos obligatorios:

| Hito | Objetivos Principales |
|-----:|-----------------------|
| **Check 1** | Conexión inicial entre módulos, sistema de logs y protocolos de comunicación |
| **Check 2** | Planificación FIFO/RR, ciclo de instrucción básico y manejo genérico de I/O |
| **Check 3** | Planificación VRR, gestión completa de memoria y sistema de archivos DialFS |

---

## 🛠️ Tecnologías Utilizadas

- **Lenguaje:** C
- **Librerías:** UTN Commons Library
- **Herramientas:** GCC, Makefiles, Valgrind, GDB
- **Protocolos:** Sockets TCP/IP, serialización y deserialización de mensajes
