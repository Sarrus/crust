<?php
// Requests the state from the CRUST socket and then reads it at a rate of one character per second. Useful to test
// synchronous writes on multiple sockets.
$connection = stream_socket_client("unix:///var/run/crust/crust.sock");
fwrite($connection, "RS\r\n");
while(TRUE)
{
    echo fread($connection, 1);
    sleep(1);
}