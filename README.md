\# Operating Systems Lab: A Simple Dropbox Clone

This project is a multi-threaded file storage server implemented in C, fulfilling the requirements for both Phase 1 and Phase 2\.

\#\# Features

\- \*\*Multi-Threaded Architecture\*\*: Uses a producer-consumer model with two distinct thread pools to separate network I/O from file system operations.  
\- \*\*Thread-Safe Queues\*\*: Employs two synchronized queues (\`ClientQueue\` and \`TaskQueue\`) with mutexes and condition variables to manage requests.  
\- \*\*User Management\*\*: Supports user \`SIGNUP\` and \`LOGIN\` functionality.  
\- \*\*File Operations\*\*: Provides \`UPLOAD\`, \`DOWNLOAD\`, \`DELETE\`, and \`LIST\` commands for authenticated users. Each user has a dedicated storage directory on the server.  
\- \*\*Concurrency Control\*\*: Implements a per-user mutex to safely handle multiple, simultaneous client sessions from the same user, preventing data corruption.  
\- \*\*Graceful Shutdown\*\*: The server can be shut down cleanly with \`Ctrl+C\`, ensuring all threads are joined and all allocated memory is freed.

\---

\#\# How to Build and Run

\#\#\# 1\. Build the Project  
A \`Makefile\` is provided to build both the server and the test client.  
\`\`\`bash  
make