# Operating Systems Lab: A Simple Dropbox Clone

This project is a multi-threaded file storage server implemented in C. It is built upon a producer-consumer architecture to handle concurrent client connections and file operations efficiently and safely. The server supports user authentication, file storage operations, and is designed with robustness and concurrency control at its core, fulfilling all requirements for both Phase 1 and Phase 2 of the project.

---

## Key Features

- **Multi-Threaded Architecture**: Utilizes two distinct thread pools (Client Pool and Worker Pool) to separate non-blocking network I/O from blocking file system operations, ensuring high responsiveness.
- **Thread-Safe Queues**: Employs two fully synchronized, bounded queues (`ClientQueue` and `TaskQueue`) using mutexes and condition variables to manage the flow of requests between thread pools without busy-waiting.
- **Complete User Management**: Supports user `SIGNUP` and `LOGIN` functionality with a simple, in-memory user database.
- **Full File Operation Suite**: Provides `UPLOAD`, `DOWNLOAD`, `DELETE`, and `LIST` commands for authenticated users. Each user is sandboxed within their own dedicated storage directory on the server.
- **Fine-Grained Concurrency Control**: Implements a **per-user mutex** to safely handle multiple, simultaneous client sessions from the same user. This prevents data corruption (e.g., deleting a file while another session is writing to it) while still allowing maximum parallelism for different users.
- **Robust Graceful Shutdown**: The server can be shut down cleanly with a `Ctrl+C` signal (`SIGINT`). It ensures all threads are properly joined and all allocated memory is freed, preventing resource leaks.

---

## Architecture Overview

The server's design is based on a three-tiered, decoupled model that ensures scalability and separation of concerns.

1.  **Main Thread (Connection Accepter)**
    - The `main` function initializes all resources and starts the thread pools.
    - It then enters an infinite loop, with its sole responsibility being to `accept()` incoming TCP connections.
    - For each new connection, it pushes the client's socket file descriptor into the `ClientQueue`.

2.  **Client Thread Pool (Session & Command Parsing)**
    - Threads in this pool wait on the `ClientQueue`.
    - When a socket is available, a thread dequeues it and becomes responsible for that client's entire session.
    - It handles the initial `SIGNUP` or `LOGIN` process.
    - For authenticated users, it reads subsequent commands (`LIST`, `UPLOAD`, etc.). Instead of executing these commands directly, it packages the request into a `Task` struct.
    - This `Task` is then pushed onto the `TaskQueue`, and the client thread waits for the task to be completed by a worker.

3.  **Worker Thread Pool (File System I/O)**
    - Threads in this pool wait on the `TaskQueue`.
    - When a `Task` is available, a worker thread dequeues it.
    - It acquires the specific user's lock to ensure exclusive access to their files.
    - It performs the heavy, blocking file system operation (e.g., reading from or writing to the disk).
    - Upon completion, it writes a response message into the `Task` struct and signals the waiting client thread.
    - It then releases the user's lock.

This decoupled design means that a slow disk operation for one user will not prevent other client threads from parsing commands and accepting new users, making the server highly responsive.

---

## Project Structure

- `server.c`: The complete source code for the multi-threaded server.
- `client.c`: The source code for an automated test client that validates core functionality.
- `Makefile`: The build script to compile the server, client, and special test executables.
- `stress_test.sh`: A shell script designed to test the server's concurrency features by simulating multiple conflicting client actions.
- `README.md`: This file.
- `.gitignore`: Specifies files and directories to be ignored by Git (e.g., executables, user data).

---

## How to Build and Test

### Prerequisites
- `gcc` compiler
- `make` build tool
- `valgrind` (for memory testing)

### 1. Build the Project
A `Makefile` is provided for easy compilation.
```bash
make
