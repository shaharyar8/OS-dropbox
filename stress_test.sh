#!/bin/bash

TEST_USER="stress_user"
TEST_PASS="password"
TEST_FILE="concurrent_file.txt"
NUM_CLIENTS=10

run_client_task() {
    CONTENT="data from client $1"
    
    {
        echo "LOGIN ${TEST_USER} ${TEST_PASS}"
        if (( $1 % 2 == 0 )); then
            echo "UPLOAD ${TEST_FILE} ${CONTENT}"
        else
            echo "DELETE ${TEST_FILE}"
        fi
        echo "LIST"
    } | nc -q 1 localhost 8080 > /dev/null
}

echo "Starting stress test with $NUM_CLIENTS concurrent clients..."

echo "Setting up test user '${TEST_USER}'..."
{
    echo "SIGNUP ${TEST_USER} ${TEST_PASS}"
} | nc -q 1 localhost 8080

for i in $(seq 1 $NUM_CLIENTS)
do
    run_client_task $i &
done

wait
echo "Stress test finished."

echo "Final state of user's directory:"
ls -l server_files/${TEST_USER}/
