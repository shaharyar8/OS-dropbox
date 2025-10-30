CC = gcc
CFLAGS = -g -Wall -pthread

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

tsan:
	$(CC) $(CFLAGS) -fsanitize=thread -o server_tsan server.c
	@echo "Built server_tsan with ThreadSanitizer."

valgrind: server
	valgrind --leak-check=full --show-leak-kinds=all ./server
stress-test: client
	@./stress_test.sh
clean:
	rm -f server client server_tsan
