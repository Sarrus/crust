<?php
$connection = stream_socket_client("unix:///var/run/crust/crust.sock");
for($i=0; $i < 100000; $i++)
{
    fwrite($connection, "IB;UM$i;\r\n");
}
fflush($connection);
sleep(1);
fclose($connection);