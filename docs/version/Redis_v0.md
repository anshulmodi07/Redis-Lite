# Redis_v0 Documentation

# Overview

This project is a V0 Redis Clone implemented in C++ using TCP sockets, threads, mutexes, and an in-memory key-value store.

Current status:

```text
Client --> TCP Socket --> Server --> unordered_map<string,string>
```

This is best described as:

**Multi-threaded TCP Key-Value Store**

rather than a full Redis clone.

---

# Architecture

## Components

### Client
Responsible for:
- Creating a TCP connection
- Sending commands
- Receiving responses

### Server
Responsible for:
- Listening for client connections
- Parsing commands
- Executing operations
- Returning responses

### Database

```cpp
unordered_map<string,string> db;
```

Acts as an in-memory key-value store.

---

# Concepts Used

## 1. Socket Programming

Creates communication between client and server.

### Server Side

```cpp
socket(AF_INET, SOCK_STREAM, 0);
```

- AF_INET = IPv4
- SOCK_STREAM = TCP

### Client Side

Connects to:

```text
127.0.0.1:8080
```

---

## 2. TCP

Reliable communication protocol.

Provides:
- Ordered delivery
- Reliable delivery
- Error checking

---

## 3. Bind

```cpp
bind(...)
```

Associates socket with port 8080.

Think:

```text
Socket + Port = Reachable Server
```

---

## 4. Listen

```cpp
listen(server_fd, 5);
```

Allows incoming connections.

Queue size:

```text
5 waiting clients
```

---

## 5. Accept

```cpp
accept(...)
```

Creates a dedicated socket for each connected client.

---

## 6. Multithreading

Each client gets its own thread.

```cpp
thread t(...);
t.detach();
```

Current architecture:

```text
Client1 --> Thread1
Client2 --> Thread2
Client3 --> Thread3
```

Advantages:
- Easy to understand
- Supports multiple clients

Limitations:
- Poor scalability
- Thousands of clients create thousands of threads

---

## 7. Mutex

```cpp
mutex db_mutex;
```

Used to prevent race conditions.

Example:

Without mutex:

```text
Thread A writes
Thread B writes
Same time
```

Possible corruption.

With mutex:

Only one thread modifies database at a time.

---

## 8. Command Parsing

Uses:

```cpp
stringstream ss(msg);
```

Input:

```text
SET name devam
```

Parsed into:

```text
command = SET
key = name
value = devam
```

---

# Current Features

## SET

Stores value.

Example:

```text
SET city delhi
```

Response:

```text
OK
```

Implementation:

```cpp
db[key] = value;
```

---

## GET

Retrieves value.

Example:

```text
GET city
```

Response:

```text
delhi
```

If key missing:

```text
NOT FOUND
```

---

# Server Execution Flow

```text
Start Server
    |
Create Socket
    |
Bind Port 8080
    |
Listen
    |
Accept Client
    |
Create Thread
    |
Receive Command
    |
Parse Command
    |
Execute
    |
Send Response
```

---

# Client Execution Flow

```text
Start Client
    |
Create Socket
    |
Connect To Server
    |
Take User Input
    |
Send Command
    |
Receive Response
    |
Exit
```

---

# End-to-End Example

Client:

```text
SET name devam
```

Server:

```text
Receive
    |
Parse
    |
db["name"]="devam"
    |
Send OK
```

Client:

```text
OK
```

Now:

```text
GET name
```

Response:

```text
devam
```

---

# Limitations of Current Version

## Single Command Client

Current client exits after:

```text
Send
Receive
Exit
```

Not interactive.

---

## No Persistence

Data stored only in RAM.

Server restart:

```text
All Data Lost
```

---

## No Expiration

Redis supports:

```text
EXPIRE
TTL
```

Current version does not.

---

## No Redis Protocol

Real Redis uses:

```text
RESP
```

Current version uses plain text.

---

## No Advanced Data Structures

Currently:

```text
String -> String
```

Only.

No:
- Lists
- Sets
- Hashes
- Sorted Sets

---

## Thread Per Client

Works for learning.

Not suitable for large scale.

Real Redis uses:

```text
epoll
Event Loop
```

---

# Resume Assessment

Current project:

```text
Socket Programming + Hash Map Database
```

Approximate Redis similarity:

```text
~10%
```

Stage:

```text
V0
```
